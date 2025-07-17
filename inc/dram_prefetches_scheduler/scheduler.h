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

  // Reset consecutive accesses for all rows in the same bank except the specified row
  void resetBankConsecutiveAccesses(const RowIdentifier& accessedRow, bool resetAll = false)
  {
    for (auto& [rowID, dramRow] : dramRowsMap_) {
      if (resetAll || (areSameBank(rowID, accessedRow) && rowID != accessedRow)) {
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

  // Query for matching row with consecutive access side effects
  bool hasMatchingRow(RowIdentifier rowID)
  {
    auto it = dramRowsMap_.find(rowID);
    bool found = (it != dramRowsMap_.end());

    if (!found) {
      // Row not in table - reset all consecutive counters for rows in this bank
      resetBankConsecutiveAccesses(rowID, false);
    }

    return found;
  }

  // Mark row as actually used
  void markRowUsed(RowIdentifier rowID, std::uint64_t now, access_type type)
  {
    auto it = dramRowsMap_.find(rowID);
    if (it != dramRowsMap_.end()) {
      std::uint64_t lat = it->second.recordAccess(now);

      // Reset consecutive accesses for all OTHER rows in this bank
      resetBankConsecutiveAccesses(rowID, false);

      // printf("[DEBUG] Row %lu:%lu:%lu:%lu:%lu marked used, consecutive=%lu\n", rowID.channel, rowID.rank, rowID.bankGroup, rowID.bank, rowID.row,
      //        it->second.getConsecutiveAccesses());

      // Increment consecutive access counter for this row
      it->second.incrementConsecutiveAccesses();

      // printf("[DEBUG] Row %lu:%lu:%lu:%lu:%lu marked used, consecutive=%lu\n", rowID.channel, rowID.rank, rowID.bankGroup, rowID.bank, rowID.row,
      //        it->second.getConsecutiveAccesses());

      // If this is at least the second consecutive access, count it
      if (it->second.getConsecutiveAccesses() >= 2) {
        ++stats.totalConsecutiveAccesses;
      }

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