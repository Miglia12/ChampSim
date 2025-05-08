// dram_request_scheduler.h
#pragma once

#include <algorithm>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

#include "dram_address_utils.h"
#include "dram_row.h"
#include "prefetch_request.h"
#include "scheduler_parameters.h"
#include "scheduler_stats.h"

namespace dram_open
{

// Define the callback type for request issuance
using request_callback_t = std::function<bool(const PrefetchRequest&)>;

class DramRequestScheduler
{
public:
  // Constructor
  DramRequestScheduler() { stats.reset(); }

  // Main scheduler function called each cycle with number_of_slots
  void processCycle(std::uint64_t currentCycle, std::size_t number_of_slots, request_callback_t try_issue)
  {
    // Prune expired requests from all rows
    for (auto& row : dramRows_) {
      stats.PRUNED_EXPIRED += row.pruneExpiredRequests(currentCycle);
    }

    // Clean up banks that can be issued to again
    busyBanks_.erase(std::remove_if(busyBanks_.begin(), busyBanks_.end(), [currentCycle](const BankStatus& bank) { return bank.readyCycle <= currentCycle; }),
                     busyBanks_.end());

    // Process multiple slots
    for (std::size_t slot = 0; slot < number_of_slots; ++slot) {
      // Find the best row to issue from
      auto bestRowIt = selectBestRow(currentCycle);
      if (bestRowIt == dramRows_.end()) {
        break; // No more valid rows to issue from
      }

      // Get the highest confidence request from the selected row
      auto request = bestRowIt->getHighestConfidenceRequest(currentCycle);

      // This should never happen because selectBestRow checks for ready requests
      if (!request.has_value()) {
        assert(false && "Something sis wrong with score calculation");
      }

      // Issue the request
      bool success = issueRequest(request.value(), try_issue);

      if (success) {
        // Clear all ready requests from this row (one address represents all requests)
        bestRowIt->clearReadyRequests(currentCycle);
        stats.ISSUED_SUCCESS ++;

        // Mark this bank as busy until currentCycle + BANK_CYCLE_DELAY
        const RowIdentifier& rowId = bestRowIt->getRowIdentifier();
        busyBanks_.push_back({rowId, currentCycle + parameters::BANK_CYCLE_DELAY});
      } else {
        // Record issue failure
        stats.ISSUE_FAILURES++;
      }
    }

    // Remove empty rows
    dramRows_.erase(std::remove_if(dramRows_.begin(), dramRows_.end(), [](const DramRow& row) { return row.getSize() == 0; }), dramRows_.end());
  }

  // Add a new prefetch request
  bool addPrefetchRequest(champsim::address address, std::uint32_t confidenceLevel, std::uint64_t issueTime, std::uint64_t currentCycle)
  {
    // Create the request
    auto request = std::make_shared<PrefetchRequest>(address, confidenceLevel, issueTime);

    // Get the row identifier for this address
    RowIdentifier rowId = to_RowIdentifier(address);

    // Calculate delay cycles
    std::uint64_t delayCycles = 0;
    if (issueTime > currentCycle) {
      delayCycles = issueTime - currentCycle;
    }

    // Find existing row
    auto rowIt = findRow(rowId);

    // Row exists - try to add request to it
    if (rowIt != dramRows_.end()) {
      bool added = rowIt->addRequest(request);
      if (added) {
        stats.REQUESTS_ADDED++;
        stats.TOTAL_DELAY_CYCLES += delayCycles;
      } else {
        stats.DROPPED_FULL++;
      }
      return added;
    }

    // Row doesn't exist - create new row with this request
    dramRows_.emplace_back(rowId, request);
    stats.REQUESTS_ADDED++;
    stats.TOTAL_DELAY_CYCLES += delayCycles;
    return true;
  }

  // Get access to the statistics
  const SchedulerStats& getStats() const { return stats; }

  // Reset statistics
  void resetStats() { stats.reset(); }

  // Calculate average row score
  float getAverageRowScore(std::uint64_t currentCycle) const noexcept
  {
    if (dramRows_.empty()) {
      return 0.0f;
    }

    float totalScore = 0.0f;
    for (const auto& row : dramRows_) {
      totalScore += row.calculateScore(currentCycle);
    }

    return totalScore / static_cast<float>(dramRows_.size());
  }

private:
  // Structure to track bank status
  struct BankStatus {
    RowIdentifier rowId;
    std::uint64_t readyCycle; // Cycle when this bank will be ready again
  };

  // Find a row by its identifier
  std::vector<DramRow>::iterator findRow(const RowIdentifier& rowId)
  {
    return std::find_if(dramRows_.begin(), dramRows_.end(), [&rowId](const DramRow& row) { return row.getRowIdentifier() == rowId; });
  }

  // Check if a bank is currently busy
  bool isBankBusy(const RowIdentifier& rowId, std::optional<champsim::address> address = std::nullopt) const
  {
    // If DRAM controller access is enabled and we have an address, use it
    if (parameters::enable_dram_controller_access && address.has_value()) {
      bool result = !MEMORY_CONTROLLER::is_bank_ready(address.value());
      // std::cout << "isBankBusy result: " << result << std::endl;
      return result;
    }

    // Otherwise use internal tracking
    return std::find_if(busyBanks_.begin(), busyBanks_.end(), [&rowId](const BankStatus& bank) { return bank.rowId == rowId; }) != busyBanks_.end();
  }

  bool wouldAddressConflict(champsim::address addr) const
  {
    if (parameters::enable_dram_controller_access) {
      return MEMORY_CONTROLLER::would_cause_bank_conflict(addr);
    }
    return false;
  }

  std::vector<DramRow>::iterator selectBestRow(std::uint64_t currentCycle)
  {
    if (dramRows_.empty()) {
      return dramRows_.end();
    }

    // Find the row with highest score that doesn't have a busy bank
    auto bestIt = dramRows_.end();
    float bestScore = -1.0f;

    for (auto it = dramRows_.begin(); it != dramRows_.end(); ++it) {
      // Skip if this row has no ready requests
      if (it->getReadyRequestCount(currentCycle) == 0) {
        continue;
      }

      // Get the highest confidence request for this row to check its address
      auto request = it->getHighestConfidenceRequest(currentCycle);
      if (!request.has_value()) {
        continue;
      }

      // Skip if this row's bank is busy
      if (isBankBusy(it->getRowIdentifier(), request.value()->getAddress())) {
        continue;
      }

      float score = it->calculateScore(currentCycle);
      if (score > bestScore) {
        bestScore = score;
        bestIt = it;
      }
    }

    return bestIt;
  }

  // Issue a prefetch request
  bool issueRequest(PrefetchRequestPtr request, request_callback_t try_issue)
  {
    // Use the callback to try to issue the request
    return try_issue(*request);
  }

  std::vector<DramRow> dramRows_;
  std::vector<BankStatus> busyBanks_; // Banks that are currently busy

  // Statistics
  SchedulerStats stats;
};

} // namespace dram