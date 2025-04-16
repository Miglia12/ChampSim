#include "next_line_ff.h"

#include <iostream>

void next_line_ff::prefetcher_initialize()
{

  row_scheduler = std::make_unique<dram_open::DramRowOpenScheduler>(SCHEDULER_QUEUE_SIZE, READY_THRESHOLD, SLACK);

  std::cout << "\nInitializing Next-Line-FF with DRAM Row Open Scheduler:\n"
            << std::left << std::setw(30) << "SCHEDULER_QUEUE_SIZE:" << SCHEDULER_QUEUE_SIZE << '\n'
            << std::setw(30) << "READY_THRESHOLD:" << READY_THRESHOLD << '\n'
            << std::setw(30) << "SLACK:" << SLACK << '\n';
}

uint32_t next_line_ff::prefetcher_cache_operate(champsim::address addr, champsim::address ip, uint8_t cache_hit, bool useful_prefetch, access_type type,
                                                uint32_t metadata_in)
{
  uint64_t current_cycle = get_current_cycle();

  // Immediate prefetch for the next block
  champsim::block_number pf_addr{addr};
  prefetch_line(champsim::address{pf_addr + 1}, true, metadata_in);

  // Add the +2 distance prefetch to the scheduler for delayed DRAM row open
  dram_open::DramRowOpenRequest row_req(champsim::address{pf_addr + 2}, // Address
                                        metadata_in                   
  );

  row_scheduler->add_request(row_req, current_cycle);

  return metadata_in;
}

uint32_t next_line_ff::prefetcher_cache_fill(champsim::address addr, long set, long way, uint8_t prefetch, champsim::address evicted_addr, uint32_t metadata_in)
{
  return metadata_in;
}

void next_line_ff::prefetcher_cycle_operate()
{
  uint64_t current_cycle = get_current_cycle();

  // Calculate how many prefetches we can issue this cycle
  std::size_t available_pq_slots = get_available_pq_slots();
  std::size_t max_issue_per_cycle = std::max(size_t{1}, static_cast<std::size_t>(static_cast<double>(available_pq_slots) * 0.8));

  // Create a callback for issuing row open requests
  auto issue_callback = [this](const dram_open::DramRowOpenRequest& req) -> bool {
    // Use the special prefetch_line with open_dram_row=true
    return this->prefetch_line(req.addr, false, req.metadata_in, true);
  };

  // Process scheduler
  row_scheduler->tick(current_cycle, max_issue_per_cycle, issue_callback);
}

void next_line_ff::prefetcher_final_stats() { row_scheduler->print_stats("Next-Line-FF DRAM Row Scheduler"); }