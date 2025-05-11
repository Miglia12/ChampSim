#pragma once
#include <cstdint>

namespace dram_open
{

/**
 * Statistics collected by the DRAM row open scheduler
 * Tracks request lifecycle and row utilization metrics
 */
struct SchedulerStats {
  // Request tracking
  std::uint64_t requestsAdded = 0;              // Requests successfully added
  std::uint64_t requestsDroppedDuplicate = 0;   // Requests dropped as duplicates
  std::uint64_t latestRequestsObserved = 0;           // Requests that have been serviced
  std::uint64_t totalLatencyLatestRequest = 0; // Total latency from ready to service

  // Row tracking
  std::uint64_t rowsCreated = 0;  // Total rows created
  std::uint64_t rowsAccessed = 0; // Rows that were accessed at least once

  /**
   * Resets all statistics to zero
   */
  void reset()
  {
    requestsAdded = 0;
    requestsDroppedDuplicate = 0;
    latestRequestsObserved = 0;
    totalLatencyLatestRequest = 0;
    rowsCreated = 0;
    rowsAccessed = 0;
  }

  /**
   * Calculates the average latency from ready to service
   * @return Average latency in cycles
   */
  double getAverageReadyToServiceLatency() const noexcept
  {
    return latestRequestsObserved ? static_cast<double>(totalLatencyLatestRequest) / static_cast<double>(latestRequestsObserved) : 0.0;
  }
};

} // namespace dram_open