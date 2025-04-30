#ifndef PREFETCHER_NEXT_LINE_FF_H
#define PREFETCHER_NEXT_LINE_FF_H

#include <cstdint>
#include <iomanip>
#include <memory>

#include "address.h"
#include "cache.h"
#include "modules.h"

struct next_line_ff : public champsim::modules::prefetcher {

  using prefetcher::prefetcher;

  void prefetcher_initialize();
  uint32_t prefetcher_cache_operate(champsim::address addr, champsim::address ip, uint8_t cache_hit, bool useful_prefetch, access_type type,
                                    uint32_t metadata_in);
  uint32_t prefetcher_cache_fill(champsim::address addr, long set, long way, uint8_t prefetch, champsim::address evicted_addr, uint32_t metadata_in);
};

#endif