#pragma once
#include <algorithm>
#include <cstdint>
#include <map>
#include <unordered_map>

#include "row_identifier.h"
#include "scheduler_histogram.h"
#include "scheduler_parameters.h"

namespace dram_open
{

struct SchedulerStats {
  // Per-interval statistics
  std::uint64_t requestsAdded = 0;
  std::uint64_t requestsDroppedDuplicate = 0;
  std::uint64_t latestRequestsObserved = 0;
  std::uint64_t totalLatencyLatestRequest = 0;

  std::uint64_t rowsCreated = 0;
  std::uint64_t rowsAccessed = 0;
  std::map<std::uint32_t, std::uint64_t> confidenceCounts;

  // Row access history for histogram generation
  struct RowHistoryEntry {
    std::uint64_t openCount = 0;
    std::uint64_t accessCount = 0;
  };
  std::unordered_map<RowIdentifier, RowHistoryEntry> rowHistory;

  // Computed histogram data (populated when stats are collected)
  RowAccessHistogram cachedHistogram;

  void reset()
  {
    requestsAdded = 0;
    requestsDroppedDuplicate = 0;
    latestRequestsObserved = 0;
    totalLatencyLatestRequest = 0;
    rowsCreated = 0;
    rowsAccessed = 0;
    confidenceCounts.clear();
    rowHistory.clear();
    cachedHistogram = RowAccessHistogram{};
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

    auto maxElement = std::max_element(confidenceCounts.begin(), confidenceCounts.end(), [](const auto& p1, const auto& p2) { return p1.second < p2.second; });

    return maxElement->first;
  }

  // Combined method that updates both per-row history and total counter
  void recordRowOpen(const RowIdentifier& rowId)
  {
    ++rowHistory[rowId].openCount;
    ++rowsCreated; // Update total counter
  }

  // Combined method that updates both per-row history and total counter
  void recordRowAccess(const RowIdentifier& rowId)
  {
    ++rowHistory[rowId].accessCount;
    ++rowsAccessed; // Update total counter
  }

  // Record successful request (not duplicate)
  void recordRequestAdded() { ++requestsAdded; }

  // Record duplicate request
  void recordRequestDropped() { ++requestsDroppedDuplicate; }

  // Record latency and confidence for an access
  void recordAccessLatency(std::uint64_t latency, std::uint32_t confidenceLevel)
  {
    totalLatencyLatestRequest += latency;
    recordUsefulConfidence(confidenceLevel);
  }

  // Compute and cache the histogram
  void computeAndCacheHistogram(const RowAccessHistogram& histogram) { cachedHistogram = histogram; }
};

} // namespace dram_open