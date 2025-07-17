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
  std::uint64_t successfulTableAccesses = 0;
  std::uint64_t totalLatencyLatestRequest = 0;

  std::uint64_t rowsCreated = 0;
  std::uint64_t rowsAccessed = 0;

  std::uint64_t successfulTableAccessesLoads = 0;
  std::uint64_t successfulTableAccessesPrefetches = 0;

  std::uint64_t totalConsecutiveAccesses = 0;

  std::map<std::uint32_t, std::uint64_t> confidenceCounts;

  void reset()
  {
    requestsAdded = 0;
    requestsDroppedDuplicate = 0;
    successfulTableAccesses = 0;
    totalLatencyLatestRequest = 0;
    rowsCreated = 0;
    rowsAccessed = 0;
    successfulTableAccessesLoads = 0;
    successfulTableAccessesPrefetches = 0;
    totalConsecutiveAccesses = 0;
    confidenceCounts.clear();
  }

  void recordUsefulConfidence(std::uint32_t confidenceLevel) noexcept { confidenceCounts[confidenceLevel]++; }

  std::uint32_t getMostUsedConfidenceLevel() const noexcept
  {
    if (confidenceCounts.empty())
      return 0;

    auto maxElement = std::max_element(confidenceCounts.begin(), confidenceCounts.end(), [](const auto& p1, const auto& p2) { return p1.second < p2.second; });

    return maxElement->first;
  }

  // Additional metrics
  double getTableHitRate() const noexcept { return rowsCreated ? static_cast<double>(rowsAccessed) / static_cast<double>(rowsCreated) : 0.0; }

  double getAverageAccessesPerUsefulRow() const noexcept
  {
    return rowsAccessed ? static_cast<double>(successfulTableAccesses) / static_cast<double>(rowsAccessed) : 0.0;
  }

  double getAverageLatencyPerAccess() const noexcept
  {
    return successfulTableAccesses ? static_cast<double>(totalLatencyLatestRequest) / static_cast<double>(successfulTableAccesses) : 0.0;
  }

  double getConsecutiveAccessRate() const noexcept
  {
    return successfulTableAccesses ? static_cast<double>(totalConsecutiveAccesses) / static_cast<double>(successfulTableAccesses) : 0.0;
  }
};
} // namespace dram_open