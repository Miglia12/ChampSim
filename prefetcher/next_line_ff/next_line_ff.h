#ifndef PREFETCHER_NEXT_LINE_FF_H
#define PREFETCHER_NEXT_LINE_FF_H

#include <cstdint>
#include <iomanip>
#include <memory>

#include "address.h"
#include "cache.h"
#include "dram_prefetches_scheduler/dram_row_open_scheduler.h"
#include "modules.h"

struct next_line_ff : public champsim::modules::prefetcher {
  // DRAM row open scheduler configuration
  static constexpr size_t SCHEDULER_QUEUE_SIZE = 126; // Maximum number of requests in the scheduler queue
  static constexpr uint64_t READY_THRESHOLD = 0;     // Cycles before a request is considered ready
  static constexpr uint64_t SLACK = 0;               // Additional cycles before pruning

  // Scheduler instance for DRAM row opens
  std::unique_ptr<dram_open::DramRowOpenScheduler> row_scheduler;

  using prefetcher::prefetcher;

  void prefetcher_initialize();
  uint32_t prefetcher_cache_operate(champsim::address addr, champsim::address ip, uint8_t cache_hit, bool useful_prefetch, access_type type,
                                    uint32_t metadata_in);
  uint32_t prefetcher_cache_fill(champsim::address addr, long set, long way, uint8_t prefetch, champsim::address evicted_addr, uint32_t metadata_in);
  void prefetcher_cycle_operate();
  void prefetcher_final_stats();

private:
  // Helper to get current cycle count
  uint64_t get_current_cycle()
  {
    return static_cast<uint64_t>(intern_->current_time.time_since_epoch() / intern_->clock_period);
  }

  // Helper to get available PQ slots
  std::size_t get_available_pq_slots()
  {
    auto pq_sizes = intern_->get_pq_size();
    auto pq_occupancies = intern_->get_pq_occupancy();
    return pq_sizes.back() - pq_occupancies.back();
  }
};

#endif