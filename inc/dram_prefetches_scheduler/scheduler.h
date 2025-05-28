#pragma once
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <memory>
#include <unordered_map>

#include "dram_row.h"
#include "prefetch_request.h"
#include "row_identifier.h"
#include "scheduler_histogram.h"
#include "scheduler_parameters.h"
#include "scheduler_stats.h"

namespace dram_open
{

class DramRequestScheduler
{
private:
  std::unordered_map<RowIdentifier, DramRow> dramRowsMap_;
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

  bool hasMatchingRow(RowIdentifier rowID, std::uint64_t now)
  {
    auto it = dramRowsMap_.find(rowID);
    if (it == dramRowsMap_.end())
      return false;

    ++stats.latestRequestsObserved;

    if (!it->second.wasAccessed()) {
      // Get all the data we need
      std::uint64_t latency = it->second.recordAccess(now);
      std::uint32_t confidence = it->second.getConfidenceLevel();

      // Update stats in one coordinated way
      stats.recordRowAccess(rowID);
      stats.recordAccessLatency(latency, confidence);

      it->second.markAccessed();
    }

    return true;
  }

  bool addPrefetchRequest(RowIdentifier rowID, champsim::address addr, std::uint32_t conf, std::uint64_t now)
  {
    auto req = std::make_shared<PrefetchRequest>(addr, conf, now);

    auto it = dramRowsMap_.find(rowID);

    if (it == dramRowsMap_.end()) {
      // New row
      dramRowsMap_.emplace(rowID, DramRow(rowID, req));

      // Update stats
      stats.recordRowOpen(rowID);
      stats.recordRequestAdded();

      return true;
    }

    // Existing row - just add the request
    if (it->second.addRequest(req)) {
      stats.recordRequestAdded();
      return true;
    }

    stats.recordRequestDropped();
    return true;
  }

  void clearAllRows()
  {
    if (parameters::SYNC_SCHEDULER_WITH_REFRESH) {
      dramRowsMap_.clear();
    }
  }

  void resetStats()
  {
    // Reset all statistics
    stats.reset();

    for (auto& [id, row] : dramRowsMap_) {
      stats.recordRowOpen(id);
      row.resetAccessedFlag();
    }
  }

  const SchedulerStats& getStats() const noexcept { return stats; }

  RowAccessHistogram computeRowAccessHistogram() const
  {
    RowAccessHistogram histogram;

    for (const auto& [rowId, history] : stats.rowHistory) {
      histogram.openHistogram.addValue(history.openCount);
      histogram.accessHistogram.addValue(history.accessCount);
    }

    assert(histogram.accessHistogram.getTotalRows() == histogram.openHistogram.getTotalRows() && "The size of the two istograms should be the same");

    return histogram;
  }
};

} // namespace dram_open