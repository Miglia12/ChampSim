#pragma once
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <memory>
#include <unordered_map>

#include "access_type.h"
#include "dram_row.h"
#include "prefetch_request.h"
#include "row_identifier.h"
#include "scheduler_parameters.h"
#include "scheduler_stats.h"

namespace dram_open
{

class DramRequestScheduler
{
private:
  std::unordered_map<RowIdentifier, DramRow> dramRowsMap_;
  std::unordered_map<RowIdentifier, std::pair<bool, std::uint64_t>> rowUsageTracking_;

  struct BankKey {
    unsigned long channel, rank, bankGroup, bank;

    bool operator==(const BankKey& other) const { return channel == other.channel && rank == other.rank && bankGroup == other.bankGroup && bank == other.bank; }
  };

  struct BankKeyHash {
    std::size_t operator()(const BankKey& k) const { return (k.channel << 12) | (k.rank << 8) | (k.bankGroup << 4) | k.bank; }
  };

  std::unordered_map<BankKey, RowIdentifier, BankKeyHash> lastTrackedRowPerBank_;

  SchedulerStats stats;
  
  DramRequestScheduler() = default;

  BankKey getBankKey(const RowIdentifier& row) const { return {row.channel, row.rank, row.bankGroup, row.bank}; }

public:
  static DramRequestScheduler& getInstance()
  {
    static DramRequestScheduler inst;
    return inst;
  }

  DramRequestScheduler(const DramRequestScheduler&) = delete;
  DramRequestScheduler& operator=(const DramRequestScheduler&) = delete;

  void track_consecutive_access(const RowIdentifier& accessedRow)
  {
    auto bank = getBankKey(accessedRow);
    auto lastRowIt = lastTrackedRowPerBank_.find(bank);

    // Check if this row is being tracked
    bool isTrackedRow = (dramRowsMap_.find(accessedRow) != dramRowsMap_.end());

    if (isTrackedRow) {
      if (lastRowIt != lastTrackedRowPerBank_.end() && lastRowIt->second == accessedRow) {
        // Same tracked row accessed consecutively
        ++stats.totalConsecutiveAccesses;
      } else {
        // First access or different tracked row - update the map
        lastTrackedRowPerBank_[bank] = accessedRow;
      }
    } else {
      // Untracked row accessed - clear any tracked row for this bank
      lastTrackedRowPerBank_.erase(bank);
    }
  }

  bool hasMatchingRow(RowIdentifier rowID)
  {
    auto it = dramRowsMap_.find(rowID);
    return (it != dramRowsMap_.end());
  }

  // Mark row as actually used (when the prediction was useful)
  void markRowUsed(RowIdentifier rowID, std::uint64_t now, access_type type)
  {
    auto it = dramRowsMap_.find(rowID);
    if (it != dramRowsMap_.end()) {
      std::uint64_t lat = it->second.recordAccess(now);

      // Update statistics for successful table access
      ++stats.successfulTableAccesses;
      stats.totalLatencyLatestRequest += lat;

      if (type == access_type::LOAD) {
        ++stats.successfulTableAccessesLoads;
      } else if (type == access_type::PREFETCH) {
        ++stats.successfulTableAccessesPrefetches;
      }

      stats.recordUsefulConfidence(it->second.getConfidenceLevel());

      // Update simulation-wide tracking
      auto& [hasBeenUseful, accessCount] = rowUsageTracking_[rowID];
      if (!hasBeenUseful) {
        hasBeenUseful = true;
      }
      ++accessCount;
    }
  }

  bool addPrefetchRequest(RowIdentifier rowID, champsim::address addr, std::uint32_t conf, std::uint64_t now)
  {
    auto req = std::make_shared<PrefetchRequest>(addr, conf, now);

    auto it = dramRowsMap_.find(rowID);

    if (it == dramRowsMap_.end()) {
      // New row in current working map
      dramRowsMap_.emplace(rowID, DramRow(rowID, req));
      ++stats.requestsAdded;

      // Initialize tracking for this row (if not already tracked)
      if (rowUsageTracking_.find(rowID) == rowUsageTracking_.end()) {
        rowUsageTracking_[rowID] = {false, 0}; // Not useful yet, 0 accesses
      }
      return true;
    }

    // Existing row in current working map
    if (it->second.addRequest(req)) {
      ++stats.requestsAdded;
      return true;
    }

    ++stats.requestsDroppedDuplicate;
    return true;
  }

  void clearAllRows()
  {
    // Clear consecutive tracking state
    lastTrackedRowPerBank_.clear();

    if (parameters::SYNC_SCHEDULER_WITH_REFRESH) {
      dramRowsMap_.clear();
    }
  }

  void resetStats()
  {
    dramRowsMap_.clear();
    lastTrackedRowPerBank_.clear();
    stats.reset();
    rowUsageTracking_.clear();
  }

  const SchedulerStats& getStats() noexcept
  {
    // Update computed statistics from centralized tracking
    stats.rowsCreated = rowUsageTracking_.size(); // All rows ever tracked
    stats.rowsAccessed =
        std::count_if(rowUsageTracking_.begin(), rowUsageTracking_.end(), [](const auto& pair) { return pair.second.first; }); // Only useful rows

    return stats;
  }

  // Additional utility methods for accessing simulation-wide row statistics
  std::uint64_t getTotalAccessesForRow(RowIdentifier rowID) const
  {
    auto it = rowUsageTracking_.find(rowID);
    return (it != rowUsageTracking_.end()) ? it->second.second : 0;
  }

  bool wasRowEverUseful(RowIdentifier rowID) const
  {
    auto it = rowUsageTracking_.find(rowID);
    return (it != rowUsageTracking_.end()) ? it->second.first : false;
  }

  bool wasRowEverCreated(RowIdentifier rowID) const { return rowUsageTracking_.find(rowID) != rowUsageTracking_.end(); }

  std::uint64_t getTotalUniqueUsefulRows() const
  {
    return std::count_if(rowUsageTracking_.begin(), rowUsageTracking_.end(), [](const auto& pair) { return pair.second.first; });
  }

  std::uint64_t getTotalUniqueCreatedRows() const { return rowUsageTracking_.size(); }

  std::uint64_t getTotalSimulationAccesses() const
  {
    std::uint64_t total = 0;
    for (const auto& [rowID, usageInfo] : rowUsageTracking_) {
      total += usageInfo.second;
    }
    return total;
  }
};

} // namespace dram_open