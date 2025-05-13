#pragma once
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <memory>
#include <vector>

#include "dram_row.h"
#include "row_identifier.h"
#include "prefetch_request.h"
#include "scheduler_parameters.h"
#include "scheduler_stats.h"

namespace dram_open
{

class DramRequestScheduler
{
private:
  std::vector<DramRow> dramRows_;
  SchedulerStats       stats;

  DramRequestScheduler() = default;

  auto findRow(const RowIdentifier& id)
  {
    return std::find_if(dramRows_.begin(), dramRows_.end(),
                       [&id](const DramRow& r){ return r.getRowIdentifier() == id; });
  }

public:
  static DramRequestScheduler& getInstance()
  {
    static DramRequestScheduler inst;
    return inst;
  }

  DramRequestScheduler(const DramRequestScheduler&)            = delete;
  DramRequestScheduler& operator=(const DramRequestScheduler&) = delete;

  bool hasMatchingRow(RowIdentifier rowID, std::uint64_t now)
  {
    auto it = findRow(rowID);
    if (it == dramRows_.end()) return false;

    // Use iterator directly
    if (!it->wasAccessed()) {         
      ++stats.rowsAccessed;
      it->markAccessed();
    }

    std::uint64_t lat = it->recordAccess(now);

    ++stats.latestRequestsObserved;
    stats.totalLatencyLatestRequest += lat;

    return true;
  }

  bool addPrefetchRequest(RowIdentifier rowID, champsim::address addr, std::uint32_t conf, std::uint64_t now)
  {
    auto req = std::make_shared<PrefetchRequest>(addr, conf, now);
    auto it = findRow(rowID);

    if (it != dramRows_.end()) {
      if (it->addRequest(req)) { 
        ++stats.requestsAdded;
        return true;
      }
      ++stats.requestsDroppedDuplicate;
      return true;
    }

    dramRows_.emplace_back(rowID, req);
    ++stats.requestsAdded;
    ++stats.rowsCreated;
    return true;
  }

  void resetStats()
  {
    // Reset statistics
    stats.reset();
    
    // Account for existing rows in statistics
    stats.rowsCreated = dramRows_.size();
    
    // Reset access flags for all rows
    for (auto& row : dramRows_) {
      row.resetAccessedFlag();
    }
  }

  const SchedulerStats& getStats() const noexcept { return stats; }
};

} // namespace dram_open