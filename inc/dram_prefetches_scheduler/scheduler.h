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
      std::uint64_t lat = it->second.recordAccess(now);
      ++stats.rowsAccessed;
      stats.totalLatencyLatestRequest += lat;

      stats.recordUsefulConfidence(it->second.getConfidenceLevel());

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
      ++stats.requestsAdded;
      ++stats.rowsCreated;
      return true;
    }

    // Existing row
    if (it->second.addRequest(req)) {
      ++stats.requestsAdded;
      return true;
    }

    ++stats.requestsDroppedDuplicate;
    return true;
  }

  void resetStats()
  {
    // Reset statistics
    stats.reset();

    stats.rowsCreated = dramRowsMap_.size();

    for (auto& [id, row] : dramRowsMap_) {
      row.resetAccessedFlag();
    }
  }

  const SchedulerStats& getStats() const noexcept { return stats; }
};

} // namespace dram_open