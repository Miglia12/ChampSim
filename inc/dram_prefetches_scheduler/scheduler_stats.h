#pragma once
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <string>
#include <string_view>

namespace dram_open
{

/**
 * Statistics collected by the DRAM row open scheduler
 */
struct SchedulerStats {
  uint64_t REQUESTS_ADDED = 0;      // Number of requests successfully added
  uint64_t DROPPED_FULL = 0;  // Number of requests dropped due to full queue
  uint64_t PRUNED_EXPIRED = 0;      // Number of requests pruned due to expiration
  uint64_t ISSUED_SUCCESS = 0;      // Number of requests successfully issued
  uint64_t ISSUE_FAILURES = 0;      // Number of requests that failed to issue
  uint64_t TOTAL_DELAY_CYCLES = 0;  // Total delay cycles of added requests

      void
      reset()
  {
    REQUESTS_ADDED = 0;
    DROPPED_FULL = 0;
    PRUNED_EXPIRED = 0;
    ISSUED_SUCCESS = 0;
    ISSUE_FAILURES = 0;
    TOTAL_DELAY_CYCLES = 0;
  }

};
}