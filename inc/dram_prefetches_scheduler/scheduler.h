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

  // Track simulation-wide statistics per row: {hasBeenUseful, totalAccessCount}
  std::unordered_map<RowIdentifier, std::pair<bool, std::uint64_t>> rowUsageTracking_;

  SchedulerStats stats;

  DramRequestScheduler() = default;

  // Helper function to check if two rows are in the same bank
  bool areSameBank(const RowIdentifier& row1, const RowIdentifier& row2) const
  {
    return row1.channel == row2.channel && row1.rank == row2.rank && row1.bankGroup == row2.bankGroup && row1.bank == row2.bank;
  }

  // Reset consecutive accesses for all tracked rows in the specified bank
  void resetBankConsecutiveAccesses(const RowIdentifier& accessedRow)
  {
    for (auto& [rowID, dramRow] : dramRowsMap_) {
      if (areSameBank(rowID, accessedRow)) {
        dramRow.resetConsecutiveAccesses();
      }
    }
  }

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
    auto it = dramRowsMap_.find(accessedRow);

    if (it == dramRowsMap_.end()) {
      // Row not in our tracking table
      resetBankConsecutiveAccesses(accessedRow);
    } else {
      // Row is in our tracking table

      // First, reset all OTHER rows in this bank
      for (auto& [rowID, dramRow] : dramRowsMap_) {
        if (areSameBank(rowID, accessedRow) && rowID != accessedRow) {
          dramRow.resetConsecutiveAccesses();
        }
      }

      // Then increment this row's counter
      it->second.incrementConsecutiveAccesses();

      // If this is at least the second consecutive access, count it
      if (it->second.getConsecutiveAccesses() >= 2) {
        ++stats.totalConsecutiveAccesses;
      }
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
    // Reset all consecutive access counters
    for (auto& [rowID, dramRow] : dramRowsMap_) {
      dramRow.resetConsecutiveAccesses();
    }
    if (parameters::SYNC_SCHEDULER_WITH_REFRESH) {
      dramRowsMap_.clear();
    }
  }

  void resetStats()
  {
    // Clear tracked rows
    dramRowsMap_.clear();

    // Reset statistics
    stats.reset();

    // Clear simulation-wide tracking for new phase/experiment
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