#pragma once
#include <algorithm>
#include <cstdint>
#include <map>

#include "scheduler_parameters.h"

namespace dram_open
{

struct SchedulerStats {
  std::uint64_t requestsAdded = 0;
  std::uint64_t requestsDroppedDuplicate = 0;
  std::uint64_t latestRequestsObserved = 0;
  std::uint64_t totalLatencyLatestRequest = 0;

  std::uint64_t rowsCreated = 0;
  std::uint64_t rowsAccessed = 0;

  std::map<std::uint32_t, std::uint64_t> confidenceCounts;

  void reset()
  {
    requestsAdded = 0;
    requestsDroppedDuplicate = 0;
    latestRequestsObserved = 0;
    totalLatencyLatestRequest = 0;
    rowsCreated = 0;
    rowsAccessed = 0;
    confidenceCounts.clear();
  }

  double getAverageReadyToServiceLatency() const noexcept
  {
    return rowsAccessed ? static_cast<double>(totalLatencyLatestRequest) / static_cast<double>(rowsAccessed) : 0.0;
  }
  
  void recordUsefulConfidence(std::uint32_t confidenceLevel) noexcept { confidenceCounts[confidenceLevel]++; }

  std::uint32_t getMostUsedConfidenceLevel() const noexcept
  {
    if (confidenceCounts.empty())
      return 0;

    auto maxElement = std::max_element(confidenceCounts.begin(), confidenceCounts.end(), [](const auto& p1, const auto& p2) {
      return p1.second < p2.second;
    });

    return maxElement->first;
  }
};
} // namespace dram_open