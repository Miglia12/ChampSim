#ifndef BERTI_DEV_H
#define BERTI_DEV_H

/*
 * Berti: an Accurate Local-Delta Data Prefetcher
 *
 * 55th ACM/IEEE International Conference on Microarchitecture (MICRO 2022),
 * October 1-5, 2022, Chicago, Illinois, USA.
 *
 * @Authors: Agustín Navarro-Torres, Biswabandan Panda, J. Alastruey-Benedé,
 *           Pablo Ibáñez, Víctor Viñals-Yúfera, and Alberto Ros
 * @Manteiners: Agustín Navarro-Torres
 * @Email: agusnt@unizar.es
 * @Date: 22/11/2022
 *
 * Port to new ChampSim framework.
 *
 * Cite this:
 *
 * A. Navarro-Torres, B. Panda, J. Alastruey-Benedé, P. Ibáñez,
 * V. Viñals-Yúfera and A. Ros,
 * "Berti: an Accurate Local-Delta Data Prefetcher,"
 * 2022 55th IEEE/ACM International Symposium on Microarchitecture (MICRO),
 * 2022, pp. 975-991, doi: 10.1109/MICRO56248.2022.00072.
 *
 * @INPROCEEDINGS{9923806,  author={Navarro-Torres, Agustín and Panda,
 * Biswabandan and Alastruey-Benedé, Jesús and Ibáñez, Pablo and Viñals-Yúfera,
 * Víctor and Ros, Alberto},  booktitle={2022 55th IEEE/ACM International
 * Symposium on Microarchitecture (MICRO)},   title={Berti: an Accurate
 * Local-Delta Data Prefetcher},   year={2022},  volume={},  number={},
 * pages={975-991},  doi={10.1109/MICRO56248.2022.00072}}
 */

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <map>
#include <queue>
#include <stdlib.h>
#include <time.h>
#include <tuple>
#include <vector>

#include "berti_dev_parameters.h"
#include "cache.h"
#include "modules.h"

using namespace berti_dev_params;

struct berti_dev : public champsim::modules::prefetcher {
  /*****************************************************************************
   *                              Stats                                        *
   *****************************************************************************/
  // Get average latency: Welford's method
  struct welford {
    uint64_t num = 0;
    float average = 0.0;
  };

  welford average_latency;

  // Get more info
  uint64_t pf_to_l1 = 0;
  uint64_t pf_to_l2 = 0;
  uint64_t pf_to_l2_bc_mshr = 0;
  uint64_t cant_track_latency = 0;
  uint64_t cross_page = 0;
  uint64_t no_cross_page = 0;
  uint64_t no_found_berti = 0;
  uint64_t found_berti = 0;
  uint64_t average_issued = 0;
  uint64_t average_num = 0;

  /*****************************************************************************
   *                      General Structs                                      *
   *****************************************************************************/

  struct Delta {
    uint64_t conf;
    int64_t delta;
    uint8_t rpl;
    Delta() : conf(0), delta(0), rpl(BERTI_R) {};
  };

  /*****************************************************************************
   *                      Berti structures                                     *
   *****************************************************************************/
  class LatencyTable
  {
    /* Latency table simulate the modified PQ and MSHR */
  private:
    struct latency_table {
      champsim::block_number addr; // Block address
      uint64_t tag = 0;            // IP-Tag
      uint64_t time = 0;           // Event cycle
      bool pf = false;             // Is the entry accessed by a demand miss
    };
    int size;

    latency_table* latencyt;

  public:
    LatencyTable(const int size);
    ~LatencyTable();

    uint8_t add(champsim::block_number addr, uint64_t tag, bool pf, uint64_t cycle);
    uint64_t get(champsim::block_number addr);
    uint64_t del(champsim::block_number addr);
    uint64_t get_tag(champsim::block_number addr);
  };

  class ShadowCache
  {
    /* Shadow cache simulate the modified L1D Cache */
  private:
    struct shadow_cache {
      champsim::block_number addr; // Block address
      uint64_t lat = 0;            // Latency
      bool pf = false;             // Is a prefetch
    }; // This struct is the vberti table

    int sets;
    int ways;
    shadow_cache** scache;

  public:
    ShadowCache(const int sets, const int ways);
    ~ShadowCache();

    bool add(uint32_t set, uint32_t way, champsim::block_number addr, bool pf, uint64_t lat);
    bool get(champsim::block_number addr);
    void set_pf(champsim::block_number addr, bool pf);
    bool is_pf(champsim::block_number addr);
    uint64_t get_latency(champsim::block_number addr);
  };

  class HistoryTable
  {
    /* History Table */
  private:
    struct history_table {
      uint64_t tag = 0;            // IP Tag
      champsim::block_number addr; // Block address accessed
      uint64_t time = 0;           // Time where the line is accessed
    }; // This struct is the history table

    const int sets = HISTORY_TABLE_SETS;
    const int ways = HISTORY_TABLE_WAYS;

    history_table** historyt;
    history_table** history_pointers;

    uint16_t get_aux(uint32_t latency, uint64_t tag, champsim::block_number act_addr, uint64_t* tags, champsim::block_number* addr, uint64_t cycle);

  public:
    HistoryTable();
    ~HistoryTable();

    int get_ways();
    void add(uint64_t tag, champsim::block_number addr, uint64_t cycle);
    uint16_t get(uint32_t latency, uint64_t tag, champsim::block_number act_addr, uint64_t* tags, champsim::block_number* addr, uint64_t cycle);
  };

  class Berti
  {
    /* Berti Table */
  private:
    struct berti {
      std::array<Delta, BERTI_TABLE_DELTA_SIZE> deltas;
      uint64_t conf = 0;
      uint64_t total_used = 0;
    };

    std::map<uint64_t, berti*> bertit;
    std::queue<uint64_t> bertit_queue;

    uint64_t size = 0;
    HistoryTable* historyt = nullptr;

    static bool compare_greater_delta(Delta a, Delta b);
    static bool compare_rpl(Delta a, Delta b);

    void increase_conf_tag(uint64_t tag);
    void conf_tag(uint64_t tag);
    void add(uint64_t tag, int64_t delta);

  public:
    Berti(uint64_t p_size);
    void set_history_table(HistoryTable* history_table) { historyt = history_table; }
    void find_and_update(uint64_t latency, uint64_t tag, uint64_t cycle, champsim::block_number line_addr);
    uint8_t get(uint64_t tag, std::vector<Delta>& res);
    uint64_t ip_hash(uint64_t ip);
  };

  LatencyTable* latencyt;
  ShadowCache* scache;
  HistoryTable* historyt;
  Berti* berti;

  // Constructor
  using prefetcher::prefetcher;

  // Required methods
  void prefetcher_initialize();
  uint32_t prefetcher_cache_operate(champsim::address addr, champsim::address ip, uint8_t cache_hit, bool useful_prefetch, access_type type,
                                    uint32_t metadata_in);
  uint32_t prefetcher_cache_fill(champsim::address addr, long set, long way, uint8_t prefetch, champsim::address evicted_addr, uint32_t metadata_in);
  void prefetcher_cycle_operate();
  void prefetcher_final_stats();
};

#endif /* BERTI_DEV_H */