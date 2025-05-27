#ifndef IP_STRIDE_TRI_H
#define IP_STRIDE_TRI_H

#include <cstdint>
#include <optional>

#include "address.h"
#include "champsim.h"
#include "modules.h"
#include "msl/lru_table.h"

struct ip_stride_tri : public champsim::modules::prefetcher {
  struct tracker_entry {
    champsim::address ip{};                                // the IP we're tracking
    champsim::block_number last_cl_addr{};                 // the last address accessed by this IP
    champsim::block_number::difference_type last_stride{}; // the stride between the last two addresses accessed by this IP

    auto index() const
    {
      using namespace champsim::data::data_literals;
      return ip.slice_upper<2_b>();
    }
    auto tag() const
    {
      using namespace champsim::data::data_literals;
      return ip.slice_upper<2_b>();
    }
  };

  struct lookahead_entry {
    champsim::address address{};
    champsim::address::difference_type stride{};
    int degree = 0; // degree remaining
  };

  constexpr static std::size_t TRACKER_SETS = 256;
  constexpr static std::size_t TRACKER_WAYS = 4;
  constexpr static int PREFETCH_DEGREE = 3;

  // DRAM row warming parameters
  constexpr static int WARM_DEGREE = 3;             // How many additional blocks to warm
  constexpr static uint32_t DEFAULT_CONFIDENCE = 1;

  // Statistics for DRAM row warming
  uint64_t dram_warm_requests = 0;
  uint64_t dram_row_warm_issued = 0;
  uint64_t dram_warm_mshr_full = 0;
  uint64_t dram_warm_cross_page = 0;
  uint64_t dram_warm_extended = 0;

  std::optional<lookahead_entry> active_lookahead;
  std::optional<lookahead_entry> extended_warm_lookahead; // Only for extended warming

  champsim::msl::lru_table<tracker_entry> table{TRACKER_SETS, TRACKER_WAYS};

  // Submit a DRAM row open request
  bool submit_dram_row_open(champsim::address addr, uint32_t confidence, uint32_t metadata);

  // Setup extended warming (beyond regular prefetch distance)
  void setup_extended_warming(champsim::address addr, champsim::address::difference_type stride);

public:
  using champsim::modules::prefetcher::prefetcher;
  void prefetcher_initialize();
  uint32_t prefetcher_cache_operate(champsim::address addr, champsim::address ip, uint8_t cache_hit, bool useful_prefetch, access_type type,
                                    uint32_t metadata_in);
  uint32_t prefetcher_cache_fill(champsim::address addr, long set, long way, uint8_t prefetch, champsim::address evicted_addr, uint32_t metadata_in);
  void prefetcher_cycle_operate();
  void prefetcher_final_stats();
};

#endif