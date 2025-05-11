#pragma once

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <memory>
#include <vector>

#include "dram_address_utils.h"
#include "dram_row.h"
#include "prefetch_request.h"
#include "scheduler_parameters.h"
#include "scheduler_stats.h"

namespace dram_open
{

/**
 * Main scheduler for DRAM requests
 * Manages rows and requests, tracks statistics, and handles row matching
 */
class DramRequestScheduler
{
public:
  // Singleton accessor methods
  static DramRequestScheduler& getInstance()
  {
    static DramRequestScheduler instance;
    return instance;
  }

  DramRequestScheduler(const DramRequestScheduler&) = delete;
  DramRequestScheduler& operator=(const DramRequestScheduler&) = delete;

  /**
   * Checks if an address matches a row with ready requests
   * @param address The memory address to check
   * @param currentCycle The current simulation cycle
   * @return true if a row with ready requests was found and accessed
   */
  bool hasMatchingRow(champsim::address address, std::uint64_t currentCycle)
  {
    RowIdentifier rowId = to_RowIdentifier(address);
    auto rowIt = findRow(rowId);

    // No matching row or no ready requests
    if (rowIt == dramRows_.end() || !rowIt->hasReadyRequests(currentCycle))
      return false;

    if (!rowIt->wasAccessed()) {
      ++stats.rowsAccessed;
      rowIt->markAccessed();
    }

    // Set the request as serviced and track latency
    auto [count, latency] = rowIt->recordAccess(currentCycle);
    stats.latestRequestsObserved ++;
    stats.totalLatencyLatestRequest += latency;

    // Clean-up row
    if (rowIt->removeServicedRequests())
      dramRows_.erase(rowIt);

    return true;
  }

  /**
   * Adds a new prefetch request
   * @param address The memory address
   * @param confidenceLevel Confidence level (0-MAX_CONFIDENCE)
   * @param readyCycle Cycle when the request will be ready
   * @param currentCycle Current simulation cycle
   * @return true if request was added successfully
   */
  bool addPrefetchRequest(champsim::address address, std::uint32_t confidenceLevel, std::uint64_t readyCycle, std::uint64_t /*currentCycle*/)
  {
    assert(confidenceLevel <= parameters::MAXIMUM_CONFIDENCE_LEVEL && "Higher than maximum confidence allows");

    // Create the request
    auto request = std::make_shared<PrefetchRequest>(address, confidenceLevel, readyCycle);

    // Get the row identifier
    RowIdentifier rowId = to_RowIdentifier(address);

    // Find if the row already exists
    auto rowIt = findRow(rowId);

    if (rowIt != dramRows_.end()) {
      // Try to add to existing row
      if (rowIt->addRequest(request)) {
        ++stats.requestsAdded;
        return true;
      }
      ++stats.requestsDroppedDuplicate;
      return false;
    }

    // Create new row with this request
    dramRows_.emplace_back(rowId, request);
    ++stats.requestsAdded;
    ++stats.rowsCreated;
    return true;
  }

  void resetStats()
  {
    stats.reset();

    std::uint64_t outstanding = 0;
    for (auto& row : dramRows_) {
      outstanding += row.outstandingRequestCount();
      row.resetAccessedFlag();
    }

    stats.requestsAdded = outstanding;
    stats.rowsCreated = static_cast<std::uint64_t>(dramRows_.size());
  }

  /**
   * Gets read-only access to the statistics
   */
  const SchedulerStats& getStats() const noexcept { return stats; }

private:
  // Private constructor for singleton
  DramRequestScheduler() = default;

  // Finds a row by its identifier
  std::vector<DramRow>::iterator findRow(const RowIdentifier& rowId)
  {
    return std::find_if(dramRows_.begin(), dramRows_.end(), [&rowId](const DramRow& row) { return row.getRowIdentifier() == rowId; });
  }

  // Storage for tracked rows
  std::vector<DramRow> dramRows_;

  // Statistics tracking
  SchedulerStats stats;
};

} // namespace dram_open