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
  uint64_t DUPLICATES_DETECTED = 0; // Number of duplicate requests detected
  uint64_t CONFIDENCE_UPDATES = 0;  // Number of confidence updates for duplicates
  uint64_t DROPPED_FULL_QUEUE = 0;  // Number of requests dropped due to full queue
  uint64_t PRUNED_EXPIRED = 0;      // Number of requests pruned due to expiration
  uint64_t ISSUED_SUCCESS = 0;      // Number of requests successfully issued
  uint64_t ISSUE_FAILURES = 0;      // Number of requests that failed to issue
  uint64_t TOTAL_DELAY_CYCLES = 0;  // Total delay cycles for all issued requests

  void reset()
  {
    REQUESTS_ADDED = 0;
    DUPLICATES_DETECTED = 0;
    CONFIDENCE_UPDATES = 0;
    DROPPED_FULL_QUEUE = 0;
    PRUNED_EXPIRED = 0;
    ISSUED_SUCCESS = 0;
    ISSUE_FAILURES = 0;
    TOTAL_DELAY_CYCLES = 0;
  }

  void print(std::string_view name) const
  {
    const uint64_t TOTAL_INPUT_REQUESTS = REQUESTS_ADDED + DUPLICATES_DETECTED;
    const uint64_t TOTAL_ATTEMPTED_ISSUES = ISSUED_SUCCESS + ISSUE_FAILURES;
    const double AVG_DELAY = ISSUED_SUCCESS > 0 ? static_cast<double>(TOTAL_DELAY_CYCLES) / static_cast<double>(ISSUED_SUCCESS) : 0.0;

    std::cout << '\n';
    std::cout << "===== " << name << " Stats =====\n\n";

    std::cout << "Request Lifecycle:\n";
    std::cout << std::left << std::setw(40) << "  Total input requests (adds + dupes):" << std::right << std::setw(12) << TOTAL_INPUT_REQUESTS << '\n';
    std::cout << std::left << std::setw(40) << "    ├─ Added to queue:" << std::right << std::setw(12) << REQUESTS_ADDED << '\n';
    std::cout << std::left << std::setw(40) << "    ├─ Dropped (queue full):" << std::right << std::setw(12) << DROPPED_FULL_QUEUE << '\n';
    std::cout << std::left << std::setw(40) << "    └─ Duplicates detected:" << std::right << std::setw(12) << DUPLICATES_DETECTED << "\n\n";

    std::cout << "Queue Results (from added requests):\n";
    std::cout << std::left << std::setw(40) << "  Issued successfully:" << std::right << std::setw(12) << ISSUED_SUCCESS << "  (" << std::fixed
              << std::setprecision(2) << (REQUESTS_ADDED ? (100.0 * static_cast<double>(ISSUED_SUCCESS) / static_cast<double>(REQUESTS_ADDED)) : 0.0) << "%)\n";
    std::cout << std::left << std::setw(40) << "  Average delay of issued requests:" << std::right << std::setw(12) << std::fixed << std::setprecision(2)
              << AVG_DELAY << " cycles\n";
    std::cout << std::left << std::setw(40) << "  Pruned (expired):" << std::right << std::setw(12) << PRUNED_EXPIRED << "  (" << std::fixed
              << std::setprecision(2) << (REQUESTS_ADDED ? (100.0 * static_cast<double>(PRUNED_EXPIRED) / static_cast<double>(REQUESTS_ADDED)) : 0.0) << "%)\n";

    uint64_t still_in_queue = REQUESTS_ADDED > ISSUED_SUCCESS + PRUNED_EXPIRED ? REQUESTS_ADDED - ISSUED_SUCCESS - PRUNED_EXPIRED : 0;

    std::cout << std::left << std::setw(40) << "  Still in queue:" << std::right << std::setw(12) << still_in_queue << "  (" << std::fixed
              << std::setprecision(2) << (REQUESTS_ADDED ? (100.0 * static_cast<double>(still_in_queue) / static_cast<double>(REQUESTS_ADDED)) : 0.0) << "%)\n";

    std::cout << "\nDuplicates:\n";
    std::cout << std::left << std::setw(40) << "  Duplicates detected:" << std::right << std::setw(12) << DUPLICATES_DETECTED << '\n';
    std::cout << std::left << std::setw(40) << "    └─ Confidence updated:" << std::right << std::setw(12) << CONFIDENCE_UPDATES << '\n';

    std::cout << "\nIssuance Attempts:\n";
    std::cout << std::left << std::setw(40) << "  Total attempted issues:" << std::right << std::setw(12) << TOTAL_ATTEMPTED_ISSUES << '\n';
    std::cout << std::left << std::setw(40) << "    ├─ Successful:" << std::right << std::setw(12) << ISSUED_SUCCESS << '\n';
    std::cout << std::left << std::setw(40) << "    └─ Failed:" << std::right << std::setw(12) << ISSUE_FAILURES << '\n';

    if (TOTAL_ATTEMPTED_ISSUES > 0) {
      std::cout << std::left << std::setw(40) << "  Issue success rate:" << std::right << std::setw(11) << std::fixed << std::setprecision(2)
                << (100.0 * static_cast<double>(ISSUED_SUCCESS) / static_cast<double>(TOTAL_ATTEMPTED_ISSUES)) << "%\n";
    }

    std::cout << std::string(55, '=') << '\n';
  }
};

} // namespace dram_open