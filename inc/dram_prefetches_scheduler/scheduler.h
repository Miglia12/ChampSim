#pragma once
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <memory>
#include <unordered_map>

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

public:
  static DramRequestScheduler& getInstance()
  {
    static DramRequestScheduler inst;
    return inst;
  }

  DramRequestScheduler(const DramRequestScheduler&) = delete;
  DramRequestScheduler& operator=(const DramRequestScheduler&) = delete;

  // Simple query for matching row - no stats updated
  bool hasMatchingRow(RowIdentifier rowID)
  {
    auto it = dramRowsMap_.find(rowID);
    return (it != dramRowsMap_.end());
  }

  // Mark row as actually used - only called when genuinely beneficial
  void markRowUsed(RowIdentifier rowID, std::uint64_t now)
  {
    auto it = dramRowsMap_.find(rowID);
    if (it != dramRowsMap_.end()) {
      std::uint64_t lat = it->second.recordAccess(now);

      // Update statistics for successful table access
      ++stats.successfulTableAccesses;
      stats.totalLatencyLatestRequest += lat;
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
    if (parameters::SYNC_SCHEDULER_WITH_REFRESH) {
      dramRowsMap_.clear();
      // printf("[SCHEDULER DEBUG] Refresh cleared %zu rows from working map, tracking map still has %zu unique rows\n", cleared_count, rowUsageTracking_.size());
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

    // Debug: Final statistics
    // printf("[SCHEDULER DEBUG] Final stats: %zu unique rows tracked, %zu useful rows, working map has %zu rows\n", stats.rowsCreated, stats.rowsAccessed,
          //  dramRowsMap_.size());

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