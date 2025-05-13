#pragma once 
#include <cstdint>

namespace dram_open
{

struct SchedulerStats {
  std::uint64_t requestsAdded             = 0;
  std::uint64_t requestsDroppedDuplicate  = 0;
  std::uint64_t latestRequestsObserved    = 0;
  std::uint64_t totalLatencyLatestRequest = 0;

  std::uint64_t rowsCreated = 0;
  std::uint64_t rowsAccessed = 0;

  void reset()
  {
    *this = {};
  }

  double getAverageReadyToServiceLatency() const noexcept
  {
    return latestRequestsObserved
             ? static_cast<double>(totalLatencyLatestRequest) /
               static_cast<double>(latestRequestsObserved)
             : 0.0;
  }
};
}