#include "next_line_ff.h"

#include <iostream>

void next_line_ff::prefetcher_initialize() { std::cout << "\nInitializing Next-Line-FF using LLC's centralized DRAM Row Open Scheduler\n"; }

uint32_t next_line_ff::prefetcher_cache_operate(champsim::address addr, champsim::address ip, uint8_t cache_hit, bool useful_prefetch, access_type type,
                                                uint32_t metadata_in)
{
  // Immediate prefetch for the next block
  champsim::block_number pf_addr{addr};
  prefetch_line(champsim::address{pf_addr + 1}, true, metadata_in);

  // Submit to LLC's scheduler
  // bool submit_dram_row_open(champsim::address addr, uint32_t confidence = 0, uint32_t metadata = 0, uint64_t ready_delay = 0) const;
  submit_dram_row_open(champsim::address{pf_addr + 2}, /*confidence=*/0, /*metadata=*/metadata_in, /*ready_delay=*/0);

  return metadata_in;
}

uint32_t next_line_ff::prefetcher_cache_fill(champsim::address addr, long set, long way, uint8_t prefetch, champsim::address evicted_addr, uint32_t metadata_in)
{
  return metadata_in;
}