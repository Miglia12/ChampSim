#include "berti_tri.h"
#include <algorithm>

/*
 * Berti: an Accurate Local-Delta Data Prefetcher
 *  
 * 55th ACM/IEEE International Conference on Microarchitecture (MICRO 2022),
 * October 1-5, 2022, Chicago, Illinois, USA.
 * 
 * @Authors: Agustín Navarro-Torres, Biswabandan Panda, J. Alastruey-Benedé, 
 *           Pablo Ibáñez, Víctor Viñals-Yúfera, and Alberto Ros
 * @Manteiners: Agustín Navarro -Torres
 * @Email: agusnt@unizar.es
 * @Date: 22/11/2022
 * 
 * This code is an update from the original code to work with the new version
 * of ChampSim: https://github.com/agusnt/Berti-Artifact
 * 
 * Maybe fine-tuning is required to get the optimal performance/accuracy.
 * 
 * Please note that this version of ChampSim has noticeable differences with 
 * the used for the paper, so results can varies.
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

using namespace berti_space;

/******************************************************************************/
/*                      Dram Prefetch functions                               */
/******************************************************************************/
uint64_t Berti::calculate_safe_delay(uint64_t current_cycle)
{
  // Calculate the minimum safe delay that ensures all regular prefetches
  // at the current cycle will reach DRAM before any low confidence prefetches

  // We need to estimate the worst-case traversal time through the cache hierarchy
  // For simplicity, we use the average latency as a baseline, plus a safety margin

  // If average latency is not yet established, use the constant
  uint64_t base_delay;
  if (average_latency.num < 10) {
    base_delay = LOW_CONF_MIN_DELAY;
  } else {
    base_delay = static_cast<uint64_t>(average_latency.average * LOW_CONF_AVG_DELAY_FACTOR);
  }

  // Enforce min/max bounds
  uint64_t safe_delay = std::max(static_cast<uint64_t>(LOW_CONF_MIN_DELAY), std::min(static_cast<uint64_t>(LOW_CONF_MAX_DELAY), safe_delay));

  if constexpr (champsim::debug_print) {
    std::cout << "[BERTI] calculate_safe_delay: base=" << base_delay << " with_margin=" << safe_delay << " avg_latency=" << average_latency.average
              << " samples=" << average_latency.num << std::endl;
  }

  return safe_delay;
}

void Berti::add_to_low_confidence_buffer(uint64_t addr, uint32_t metadata, uint64_t current_cycle, uint64_t confidence)
{
  // Calculate delay
  uint64_t delay = calculate_safe_delay(current_cycle);

  delayed_prefetch new_pf = {addr, metadata,
                             current_cycle + delay,
                             confidence, current_cycle};

  // Check for duplicates
  auto duplicate = std::find_if(low_confidence_buffer.begin(), low_confidence_buffer.end(),
                                [addr](const delayed_prefetch& pf) { return (pf.addr >> LOG2_BLOCK_SIZE) == (addr >> LOG2_BLOCK_SIZE); });

  if (duplicate != low_confidence_buffer.end()) {
    // If already queued, keep the higher confidence version
    if (confidence > duplicate->confidence) {
      duplicate->confidence = confidence;
      // Don't update timing
    }
    return;
  }

  if (low_confidence_buffer.size() >= LOW_CONF_BUFFER_SIZE) {
    // Find lowest priority prefetch
    auto lowest_priority =
        std::min_element(low_confidence_buffer.begin(), low_confidence_buffer.end(), [current_cycle](const delayed_prefetch& a, const delayed_prefetch& b) {
          double priority_a = a.confidence - ((a.ready_cycle > current_cycle) ? (a.ready_cycle - current_cycle) * LOW_CONF_PENALTY_PER_CYCLE : 0);

          double priority_b = b.confidence - ((b.ready_cycle > current_cycle) ? (b.ready_cycle - current_cycle) * LOW_CONF_PENALTY_PER_CYCLE : 0);

          return priority_a < priority_b; // Lower priority = candidate for replacement
        });

    
    double new_priority = confidence - ((new_pf.ready_cycle > current_cycle) ? (new_pf.ready_cycle - current_cycle) * LOW_CONF_PENALTY_PER_CYCLE : 0);

    // Calculate lowest existing priority
    double lowest_priority_value =
        lowest_priority->confidence
        - ((lowest_priority->ready_cycle > current_cycle) ? (lowest_priority->ready_cycle - current_cycle) * LOW_CONF_PENALTY_PER_CYCLE : 0);

    // Replace only if new prefetch has higher priority
    if (new_priority > lowest_priority_value) {
      *lowest_priority = new_pf;
    }
  } else {
    // Buffer not full
    low_confidence_buffer.push_back(new_pf);
  }

  if constexpr (champsim::debug_print) {
    std::cout << "[BERTI] buffered low-confidence prefetch: " << std::hex << addr << std::dec << " confidence: " << confidence << " delay: " << delay
              << " ready at: " << new_pf.ready_cycle << " (current: " << current_cycle << ")" << std::endl;
  }
}

void Berti::issue_delayed_prefetches(uint64_t current_cycle, CACHE* cache_ptr)
{
  if (!CACHE::llc_static)
    return;

  // First, remove late prefetches that have missed their timing window
  auto stale_end = std::remove_if(low_confidence_buffer.begin(), low_confidence_buffer.end(), [current_cycle](const delayed_prefetch& pf) {
    // Remove if it's beyond ready_cycle + slack
    return current_cycle > (pf.ready_cycle + LOW_CONF_LATE_SLACK);
  });

  size_t late_count = std::distance(stale_end, low_confidence_buffer.end());
  if (late_count > 0) {
    late_removed += late_count;

    if constexpr (champsim::debug_print) {
      std::cout << "[BERTI] removed " << late_count << " late prefetches (beyond timing window) from buffer" << std::endl;
    }
    low_confidence_buffer.erase(stale_end, low_confidence_buffer.end());
  }

  // Check if LLC queues are too full to issue more prefetches
  bool bandwidth_available = true;
  if (CACHE::llc_static) {
    // Check LLC queue occupancy
    double pq_occupancy = 0.0;
    auto pq_sizes = CACHE::llc_static->get_pq_size();
    auto pq_occup = CACHE::llc_static->get_pq_occupancy();

    for (size_t i = 0; i < pq_sizes.size(); i++) {
      if (pq_sizes[i] > 0) {
        pq_occupancy = std::max(pq_occupancy, static_cast<double>(pq_occup[i]) / pq_sizes[i] * 100.0);
      }
    }

    bandwidth_available = (pq_occupancy < LOW_CONF_BANDWIDTH_THRESHOLD);

    if (!bandwidth_available) {
      throttled_prefetches++;

      if constexpr (champsim::debug_print) {
        std::cout << "[BERTI] throttling low-confidence prefetches due to high LLC PQ occupancy: " << pq_occupancy << "%" << std::endl;
      }
    }
  }

  // If bandwidth is available, find prefetches ready to issue
  if (bandwidth_available) {
    std::vector<size_t> to_issue;

    for (size_t i = 0; i < low_confidence_buffer.size(); i++) {
      if (low_confidence_buffer[i].ready_cycle <= current_cycle) {
        to_issue.push_back(i);
      }
    }

    // Issue ready prefetches
    for (auto idx = to_issue.rbegin(); idx != to_issue.rend(); ++idx) {
      const auto& pf = low_confidence_buffer[*idx];

      if constexpr (champsim::debug_print) {
        std::cout << "[BERTI] issuing delayed prefetch: " << std::hex << pf.addr << std::dec << " confidence: " << pf.confidence
                  << " ready cycle: " << pf.ready_cycle << " delay: " << (pf.ready_cycle - pf.creation_cycle) << " (current: " << current_cycle << ")"
                  << std::endl;
      }

      // Issue to LLC
      if(CACHE::llc_static->prefetch_line(pf.addr, false, pf.metadata, false))
        dram_prefetch_issued++;
    }

    // Remove issued prefetches
    for (auto idx = to_issue.rbegin(); idx != to_issue.rend(); ++idx) {
      low_confidence_buffer.erase(low_confidence_buffer.begin() + *idx);
    }
  }
}

uint8_t Berti::get_dram_prefetch_candidates(uint64_t tag, std::vector<delta_t>& res, uint8_t confidence_threshold)
{
  if (!bertit.count(tag)) {
    if constexpr (champsim::debug_print)
      std::cout << " TAG NOT FOUND" << std::endl;
    return 0;
  }

  if constexpr (champsim::debug_print)
    std::cout << std::endl;

  // We found the tag
  berti* entry = bertit[tag];

  // Add only BERTI_R deltas with confidence >= threshold
  for (auto& i : entry->deltas) {
    if (i.delta != 0 && i.rpl == BERTI_R && i.conf >= confidence_threshold) {
      res.push_back(i);
    }
  }

#if DRAM_PREFETCH_SORT
  // Sort the entries by confidence (higher first), then by delta size
  if (!res.empty()) {
    std::sort(std::begin(res), std::end(res), [](const delta_t& a, const delta_t& b) {
      if (a.conf > b.conf)
        return true; // Higher confidence first
      if (a.conf < b.conf)
        return false;

      // If confidence is equal, prefer smaller deltas
      return std::abs(a.delta) < std::abs(b.delta);
    });
  }
#endif

  return 1;
}

/******************************************************************************/
/*                      Latency table functions                               */
/******************************************************************************/
uint8_t LatencyTable::add(uint64_t addr, uint64_t tag, bool pf, uint64_t cycle)
{
  /*
   * Save if possible the new miss into the pqmshr (latency) table
   *
   * Parameters:
   *  - addr: address without cache offset
   *  - access: is theh entry accessed by a demand request
   *  - cycle: time to use in the latency table
   *
   * Return: pf
   */

  if constexpr (champsim::debug_print) 
  {
    std::cout << "[BERTI_LATENCY_TABLE] " << __func__;
    std::cout << " addr: " << std::hex << addr << " tag: " << tag;
    std::cout << " prefetch: " << std::dec << +pf << " cycle: " << cycle;
  }

  latency_table *free;
  free   = nullptr;

  for (int i = 0; i < size; i++)
  {
    // Search if the addr already exists. If it exist we does not have
    // to do nothing more
    if (latencyt[i].addr == addr)
    {
      if constexpr (champsim::debug_print) 
      {
        std::cout << " line already found; find_tag: " << latencyt[i].tag;
        std::cout << " find_pf: " << +latencyt[i].pf << std::endl;
      }
      // latencyt[i].time = cycle;
      latencyt[i].pf   = pf;
      latencyt[i].tag  = tag;
      return latencyt[i].pf;
    }

    // We discover a free space into the latency table, save it for later
    if (latencyt[i].tag == 0) free = &latencyt[i];
  }

  if (free == nullptr) assert(0 && "No free space latency table");

  // We save the new entry into the latency table
  free->addr = addr;
  free->time = cycle;
  free->tag  = tag;
  free->pf   = pf;

  if constexpr (champsim::debug_print) std::cout << " new entry" << std::endl;
  return free->pf;
}

uint64_t LatencyTable::del(uint64_t addr)
{
  /*
   * Remove the address from the latency table
   *
   * Parameters:
   *  - addr: address without cache offset
   *
   *  Return: the latency of the address
   */

  if constexpr (champsim::debug_print) 
  {
    std::cout << "[BERTI_LATENCY_TABLE] " << __func__;
    std::cout << " addr: " << std::hex << addr;
  }

  for (int i = 0; i < size; i++)
  {
    // Line already in the table
    if (latencyt[i].addr == addr)
    {
      // Calculate latency
      uint64_t time = latencyt[i].time;

      if constexpr (champsim::debug_print)
      {
        std::cout << " tag: " << latencyt[i].tag;
        std::cout << " prefetch: " << std::dec << +latencyt[i].pf;
        std::cout << " cycle: " << latencyt[i].time << std::endl;
      }

      latencyt[i].addr = 0; // Free the entry
      latencyt[i].tag  = 0; // Free the entry
      latencyt[i].time = 0; // Free the entry
      latencyt[i].pf   = 0; // Free the entry

      // Return the latency
      return time;
    }
  }

  // We should always track the misses
  if constexpr (champsim::debug_print) std::cout << " TRANSLATION" << std::endl;
  return 0;
}

uint64_t LatencyTable::get(uint64_t addr)
{
  /*
   * Return time or 0 if the addr is or is not in the pqmshr (latency) table
   *
   * Parameters:
   *  - addr: address without cache offset
   *
   * Return: time if the line is in the latency table, otherwise 0
   */

  if constexpr (champsim::debug_print)
  {
    std::cout << "[BERTI_LATENCY_TABLE] " << __func__;
    std::cout << " addr: " << std::hex << addr << std::dec;
  }

  for (int i = 0; i < size; i++)
  {
    // Search if the addr already exists
    if (latencyt[i].addr == addr)
    {
      if constexpr (champsim::debug_print)
      {
        std::cout << " time: " << latencyt[i].time << std::endl;
      }
      return latencyt[i].time;
    }
  }

  if constexpr (champsim::debug_print) std::cout << " NOT FOUND" << std::endl;
  return 0;
}

uint64_t LatencyTable::get_tag(uint64_t addr)
{
  /*
   * Return IP-Tag or 0 if the addr is or is not in the pqmshr (latency) table
   *
   * Parameters:
   *  - addr: address without cache offset
   *
   * Return: ip-tag if the line is in the latency table, otherwise 0
   */

  if constexpr (champsim::debug_print) 
  {
    std::cout << "[BERTI_LATENCY_TABLE] " << __func__;
    std::cout << " addr: " << std::hex << addr;
  }

  for (int i = 0; i < size; i++)
  {
    if (latencyt[i].addr == addr && latencyt[i].tag) // This is the address
    {
      if constexpr (champsim::debug_print) 
      {
        std::cout << " tag: " << latencyt[i].tag << std::endl;
      }
      return latencyt[i].tag;
    }
  }

  if constexpr (champsim::debug_print) std::cout << " NOT_FOUND" << std::endl;
  return 0;
}

/******************************************************************************/
/*                       Shadow Cache functions                               */
/******************************************************************************/
bool ShadowCache::add(uint32_t set, uint32_t way, uint64_t addr, bool pf, uint64_t lat)
{
  /*
   * Add block to shadow cache
   *
   * Parameters:
   *      - cpu: cpu
   *      - set: cache set
   *      - way: cache way
   *      - addr: cache block v_addr
   *      - access: the cache is access by a demand
   */

  if constexpr (champsim::debug_print)
  {
    std::cout << "[BERTI_SHADOW_CACHE] " << __func__;
    std::cout << " set: " << set << " way: " << way;
    std::cout << " addr: " << std::hex << addr << std::dec;
    std::cout << " pf: " << +pf;
    std::cout << " latency: " << lat << std::endl;
  }

  scache[set][way].addr = addr;
  scache[set][way].pf   = pf;
  scache[set][way].lat  = lat;
  return scache[set][way].pf;
}

bool ShadowCache::get(uint64_t addr)
{
  /*
   * Parameters:
   *      - addr: cache block v_addr
   *
   * Return: true if the addr is in the l1d cache, false otherwise
   */

  if constexpr (champsim::debug_print)
  {
    std::cout << "[BERTI_SHADOW_CACHE] " << __func__;
    std::cout << " addr: " << std::hex << addr << std::endl;
  }

  for (int i = 0; i < sets; i++)
  {
    for (int ii = 0; ii < ways; ii++)
    {
      if (scache[i][ii].addr == addr) 
      {
        if constexpr (champsim::debug_print)
        {
          std::cout << " set: " << i << " way: " << i << std::endl;
        }
        return true;
      }
    }
  }

  return false;
}

void ShadowCache::set_pf(uint64_t addr, bool pf)
{
  /*
   * Parameters:
   *      - addr: cache block v_addr
   *
   * Return: change value of pf field 
   */
  if constexpr (champsim::debug_print)
  {
    std::cout << "[BERTI_SHADOW_CACHE] " << __func__;
    std::cout << " addr: " << std::hex << addr << std::dec;
  }

  for (int i = 0; i < sets; i++)
  {
    for (int ii = 0; ii < ways; ii++)
    {
      if (scache[i][ii].addr == addr) 
      {
        if constexpr (champsim::debug_print)
        {
          std::cout << " set: " << i << " way: " << ii;
          std::cout << " old_pf_value: " << +scache[i][ii].pf;
          std::cout << " new_pf_value: " << +pf << std::endl;
        }
        scache[i][ii].pf = pf;
        return;
      }
    }
  }

  // The address should always be in the cache
  assert((0) && "Address is must be in shadow cache");
}

bool ShadowCache::is_pf(uint64_t addr)
{
  /*
   * Parameters:
   *      - addr: cache block v_addr
   *
   * Return: True if the saved one is a prefetch
   */

  if constexpr (champsim::debug_print)
  {
    std::cout << "[BERTI_SHADOW_CACHE] " << __func__;
    std::cout << " addr: " << std::hex << addr << std::dec;
  }

  for (int i = 0; i < sets; i++)
  {
    for (int ii = 0; ii < ways; ii++)
    {
      if (scache[i][ii].addr == addr)
      {
        if constexpr (champsim::debug_print)
        {
          std::cout << " set: " << i << " way: " << ii;
          std::cout << " pf: " << +scache[i][ii].pf << std::endl;
        }

        return scache[i][ii].pf;
      }
    }
  }

  assert((0) && "Address is must be in shadow cache");
  return 0;
}

uint64_t ShadowCache::get_latency(uint64_t addr)
{
  /*
   * Init shadow cache
   *
   * Parameters:
   *      - addr: cache block v_addr
   *
   * Return: the saved latency
   */
  if constexpr (champsim::debug_print)
  {
    std::cout << "[BERTI_SHADOW_CACHE] " << __func__;
    std::cout << " addr: " << std::hex << addr << std::dec;
  }

  for (int i = 0; i < sets; i++)
  {
    for (int ii = 0; ii < ways; ii++)
    {
      if (scache[i][ii].addr == addr) 
      {
        if constexpr (champsim::debug_print)
        {
          std::cout << " set: " << i << " way: " << ii;
          std::cout << " latency: " << scache[i][ii].lat << std::endl;
        }

        return scache[i][ii].lat;
      }
    }
  }

  assert((0) && "Address is must be in shadow cache");
  return 0;
}

/******************************************************************************/
/*                       History Table functions                               */
/******************************************************************************/
void HistoryTable::add(uint64_t tag, uint64_t addr, uint64_t cycle)
{
  /*
   * Save the new information into the history table
   *
   * Parameters:
   *  - tag: PC tag
   *  - addr: addr access
   */
  uint16_t set = tag & TABLE_SET_MASK;
  // If the latest entry is the same, we do not add it
  if (history_pointers[set] == &historyt[set][ways - 1])
  {
    if (historyt[set][0].addr == (addr & ADDR_MASK)) return;
  } else if ((history_pointers[set] - 1)->addr == (addr & ADDR_MASK)) return;

  // Save new element into the history table
  history_pointers[set]->tag       = tag;
  history_pointers[set]->time      = cycle & TIME_MASK;
  history_pointers[set]->addr      = addr & ADDR_MASK;

  if constexpr (champsim::debug_print)
  {
    std::cout << "[BERTI_HISTORY_TABLE] " << __func__;
    std::cout << " tag: " << std::hex << tag << " line_addr: " << addr << std::dec;
    std::cout << " cycle: " << cycle << " set: " << set << std::endl;
  }

  if (history_pointers[set] == &historyt[set][ways - 1])
  {
    history_pointers[set] = &historyt[set][0]; // End the cycle
  } else history_pointers[set]++; // Pointer to the next (oldest) entry
}

uint16_t HistoryTable::get_aux(uint32_t latency, 
    uint64_t tag, uint64_t act_addr, uint64_t *tags, uint64_t *addr, 
    uint64_t cycle)
{
  /*
   * Return an array (by parameter) with all the possible PC that can launch
   * an on-time and late prefetch
   *
   * Parameters:
   *  - tag: PC tag
   *  - latency: latency of the processor
   */

  uint16_t num_on_time = 0;
  uint16_t set = tag & TABLE_SET_MASK;

  if constexpr (champsim::debug_print)
  {
    std::cout << "[BERTI_HISTORY_TABLE] " << __func__;
    std::cout << " tag: " << std::hex << tag << " line_addr: " << addr << std::dec;
    std::cout << " cycle: " << cycle << " set: " << set << std::endl;
  }

  // This is the begin of the simulation
  if (cycle < latency) return num_on_time;

  // The IPs that is launch in this cycle will be able to launch this prefetch
  cycle -= latency; 

  // Pointer to guide
  history_table *pointer = history_pointers[set];

  do
  {
    // Look for the IPs that can launch this prefetch
    if (pointer->tag == tag && pointer->time <= cycle)
    {
      // Test that addr is not duplicated
      if (pointer->addr == act_addr) return num_on_time;

      // This IP can launch the prefetch
      tags[num_on_time] = pointer->tag;
      addr[num_on_time] = pointer->addr;
      num_on_time++;
    }

    if (pointer == historyt[set])
    {
      // We get at the end of the history, we start again
      pointer = &historyt[set][ways - 1];
    } else pointer--;
  } while (pointer != history_pointers[set]);

  return num_on_time;
}

uint16_t HistoryTable::get(uint32_t latency, uint64_t tag, uint64_t act_addr,
    uint64_t *tags, uint64_t *addr, uint64_t cycle)
{
  /*
   * Return an array (by parameter) with all the possible PC that can launch
   * an on-time and late prefetch
   *
   * Parameters:
   *  - tag: PC tag
   *  - latency: latency of the processor
   *  - on_time_ip (out): ips that can launch an on-time prefetch
   *  - on_time_addr (out): addr that can launch an on-time prefetch
   *  - num_on_time (out): number of ips that can launch an on-time prefetch
   */

  act_addr &= ADDR_MASK;

  uint16_t num_on_time = get_aux(latency, tag, act_addr, tags, addr, cycle & TIME_MASK);

  // We found on-time prefetchs
  return num_on_time;
}

/******************************************************************************/
/*                        Berti table functions                               */
/******************************************************************************/
void Berti::increase_conf_tag(uint64_t tag)
{
  /*
   * Increase the global confidence of the deltas associated to the tag
   *
   * Parameters:
   *  tag : tag to find
   */
  if constexpr (champsim::debug_print)
    std::cout << "[BERTI_BERTI] " << __func__ << " tag: " << std::hex << tag << std::dec;

  if (bertit.find(tag) == bertit.end())
  {
    // Tag not found
    if constexpr (champsim::debug_print) 
      std::cout << " TAG NOT FOUND" << std::endl;

    return;
  }

  // Get the entries and the deltas
  bertit[tag]->conf += CONFIDENCE_INC;

  if constexpr (champsim::debug_print) 
    std::cout << " global_conf: " << bertit[tag]->conf;

  if (bertit[tag]->conf == CONFIDENCE_MAX) 
  {
    // Max confidence achieve - track and update delta confidence levels
    for (auto &i: bertit[tag]->deltas)
    {
      // Track previous replacement level before setting new one
      uint8_t prev_rpl = i.rpl;
      
      // Set bits to prefetch level
      if (i.conf > CONFIDENCE_L1)i.rpl = BERTI_L1;
      else if (i.conf > CONFIDENCE_L2) i.rpl = BERTI_L2;
      else if (i.conf > CONFIDENCE_L2R) i.rpl = BERTI_L2R;
      else i.rpl = BERTI_R;

      if constexpr (champsim::debug_print) 
      {
        std::cout << "Delta: " << i.delta;
        std::cout << " Conf: "  << i.conf << " Level: " << +i.rpl;
        std::cout << "|";
      }

      i.conf = 0; // Reset confidence
    }

    bertit[tag]->conf = 0; // Reset global confidence
  }

  if constexpr (champsim::debug_print) std::cout << std::endl;
}

void Berti::add(uint64_t tag, int64_t delta)
{
  /*
   * Save the new information into the history table
   *
   * Parameters:
   *  - tag: PC tag
   *  - cpu: actual cpu
   *  - stride: actual cpu
   */
  if constexpr (champsim::debug_print)
  {
    std::cout << "[BERTI_BERTI] " << __func__;
    std::cout << " tag: " << std::hex << tag << std::dec;
    std::cout << " delta: " << delta;
  }

  auto add_delta = [](auto delta, auto entry)
  {
    // Lambda function to add a new element
    delta_t new_delta;
    new_delta.delta = delta;
    new_delta.conf = CONFIDENCE_INIT;
    new_delta.rpl = BERTI_R;
    auto it = std::find_if(std::begin(entry->deltas), std::end(entry->deltas), [](const auto i){
      return (i.delta == 0);
    });
    assert(it != std::end(entry->deltas));
    *it = new_delta;
  };

  if (bertit.find(tag) == bertit.end())
  {
    if constexpr (champsim::debug_print)
      std::cout << " allocating a new entry;";

    // We are not tracking this tag
    if (bertit_queue.size() > BERTI_TABLE_SIZE)
    {
      // FIFO replacent algorithm
      uint64_t key = bertit_queue.front();
      berti *entry = bertit[key];

      if constexpr (champsim::debug_print)
        std::cout << " removing tag: " << std::hex << key << std::dec << ";";

      delete entry; // Free previous entry

      bertit.erase(bertit_queue.front());
      bertit_queue.pop();
    }

    bertit_queue.push(tag); // Add new tag
    assert((bertit.size() <= BERTI_TABLE_SIZE) && "Tracking too much tags");

    // Confidence IP
    berti *entry = new berti;
    entry->conf = CONFIDENCE_INC;

    // Saving the new stride
    add_delta(delta, entry);

    if constexpr (champsim::debug_print)
      std::cout << " confidence: " << CONFIDENCE_INIT << std::endl;

    // Save the new tag
    bertit.insert(std::make_pair(tag, entry));
    return;
  }

  // Get the delta
  berti *entry  = bertit[tag];

  for (auto &i: entry->deltas)
  {
    if (i.delta == delta)
    {
      // We already track the delta
      i.conf += CONFIDENCE_INC;

      if (i.conf > CONFIDENCE_MAX) i.conf = CONFIDENCE_MAX;

      if constexpr (champsim::debug_print)
        std::cout << " confidence: " << i.conf << std::endl;

      return;
    }
  }

  // We have space to add a new entry
  auto ssize = std::count_if(std::begin(entry->deltas), std::end(entry->deltas),[](const auto i){
    return i.delta != 0;
  });

  if (ssize < size)
  {
    add_delta(delta, entry);
    assert((std::size(entry->deltas) <= size) && "I remember too much deltas");
    return;
  }

  // We find the delta with less confidence
  std::sort(std::begin(entry->deltas), std::end(entry->deltas), compare_rpl);
  if (entry->deltas.front().rpl == BERTI_R || entry->deltas.front().rpl == BERTI_L2R) 
  {
    if constexpr (champsim::debug_print)
      std::cout << " replaced_delta: " << entry->deltas.front().delta << std::endl;
    entry->deltas.front().delta = delta;
    entry->deltas.front().conf = CONFIDENCE_INIT;
    entry->deltas.front().rpl = BERTI_R;
  }
}

uint8_t Berti::get(uint64_t tag, std::vector<delta_t> &res)
{
  /*
   * Save the new information into the history table
   *
   * Parameters:
   *  - tag: PC tag
   *
   * Return: the stride to prefetch
   */
  if constexpr (champsim::debug_print)
  {
    std::cout << "[BERTI_BERTI] " << __func__ << " tag: " << std::hex << tag;
    std::cout << std::dec;
  }

  if (!bertit.count(tag))
  {
    if constexpr (champsim::debug_print)
      std::cout << " TAG NOT FOUND" << std::endl;
    no_found_berti++;
    return 0;
  }
  found_berti++;

  if constexpr (champsim::debug_print) std::cout << std::endl;

  // We found the tag
  berti *entry = bertit[tag];
  
  // Add non-zero deltas with high confidence to the result vector
  for (auto &i: entry->deltas) {
    if (i.delta != 0 && i.rpl != BERTI_R) {
      res.push_back(i);
    }
  }

  if (res.empty() && entry->conf >= LAUNCH_MIDDLE_CONF)
  {
    // We do not find any delta, so we will try to launch with small confidence
    for (auto &i: entry->deltas)
    {
      if (i.delta != 0)
      {
        delta_t new_delta;
        new_delta.delta = i.delta;
        if (i.conf > CONFIDENCE_MIDDLE_L1) new_delta.rpl = BERTI_L1;
        else if (i.conf > CONFIDENCE_MIDDLE_L2) new_delta.rpl = BERTI_L2;
        else continue;
        res.push_back(new_delta);
      }
    }
  }

  // Sort the entries
  std::sort(std::begin(res), std::end(res), compare_greater_delta);
  return 1;
}

void Berti::find_and_update(uint64_t latency, uint64_t tag, uint64_t cycle, uint64_t line_addr)
{ 
  // We were tracking this miss
  uint64_t tags[HISTORY_TABLE_WAYS];
  uint64_t addr[HISTORY_TABLE_WAYS];
  uint16_t num_on_time = 0;

  // Get the IPs that can launch a prefetch
  num_on_time = historyt->get(latency, tag, line_addr, tags, addr, cycle);

  for (uint32_t i = 0; i < num_on_time; i++)
  {
    // Increase conf tag
    if (i == 0) increase_conf_tag(tag);

    // Add information into berti table
    int64_t stride;
    line_addr &= ADDR_MASK;

    // Usually applications go from lower to higher memory position.
    // The operation order is important (mainly because we allow
    // negative strides)
    stride = (int64_t) (line_addr - addr[i]);

    if ((std::abs(stride) < (1 << DELTA_MASK))) add(tags[i], stride); 
  }
}

bool Berti::compare_rpl(delta_t a, delta_t b)
{
  if (a.rpl == BERTI_R && b.rpl != BERTI_R) return 1;
  else if (b.rpl == BERTI_R && a.rpl != BERTI_R) return 0;
  else if (a.rpl == BERTI_L2R && b.rpl != BERTI_L2R) return 1;
  else if (b.rpl == BERTI_L2R && a.rpl != BERTI_L2R) return 0;
  else
  {
    if (a.conf < b.conf) return 1;
    else return 0;
  }
}

bool Berti::compare_greater_delta(delta_t a, delta_t b)
{
  // Sorted stride when the confidence is full
  if (a.rpl == BERTI_L1 && b.rpl != BERTI_L1) return 1;
  else if (a.rpl != BERTI_L1 && b.rpl == BERTI_L1) return 0;
  else
  {
    if (a.rpl == BERTI_L2 && b.rpl != BERTI_L2) return 1;
    else if (a.rpl != BERTI_L2 && b.rpl == BERTI_L2) return 0;
    else
    {
      if (a.rpl == BERTI_L2R && b.rpl != BERTI_L2R) return 1;
      if (a.rpl != BERTI_L2R && b.rpl == BERTI_L2R) return 0;
      else
      {
        if (std::abs(a.delta) < std::abs(b.delta)) return 1;
        return 0;
      }
    }
  }
}

uint64_t Berti::ip_hash(uint64_t ip)
{
  /*
   * IP hash function
   */
#ifdef HASH_ORIGINAL
  ip = ((ip >> 1) ^ (ip >> 4)); // Original one
#endif
  // IP hash from here: http://burtleburtle.net/bob/hash/integer.html
#ifdef THOMAS_WANG_HASH_1
  ip = (ip ^ 61) ^ (ip >> 16);
  ip = ip + (ip << 3);
  ip = ip ^ (ip >> 4);
  ip = ip * 0x27d4eb2d;
  ip = ip ^ (ip >> 15);
#endif
#ifdef THOMAS_WANG_HASH_2
  ip = (ip+0x7ed55d16) + (ip<<12);
  ip = (ip^0xc761c23c) ^ (ip>>19);
  ip = (ip+0x165667b1) + (ip<<5);
  ip = (ip+0xd3a2646c) ^ (ip<<9);
  ip = (ip+0xfd7046c5) + (ip<<3);
  ip = (ip^0xb55a4f09) ^ (ip>>16);
#endif
#ifdef THOMAS_WANG_HASH_3
  ip -= (ip<<6);
  ip ^= (ip>>17);
  ip -= (ip<<9);
  ip ^= (ip<<4);
  ip -= (ip<<3);
  ip ^= (ip<<10);
  ip ^= (ip>>15);
#endif
#ifdef THOMAS_WANG_HASH_4
  ip += ~(ip<<15);
  ip ^=  (ip>>10);
  ip +=  (ip<<3);
  ip ^=  (ip>>6);
  ip += ~(ip<<11);
  ip ^=  (ip>>16);
#endif
#ifdef THOMAS_WANG_HASH_5
  ip = (ip+0x479ab41d) + (ip<<8);
  ip = (ip^0xe4aa10ce) ^ (ip>>5);
  ip = (ip+0x9942f0a6) - (ip<<14);
  ip = (ip^0x5aedd67d) ^ (ip>>3);
  ip = (ip+0x17bea992) + (ip<<7);
#endif
#ifdef THOMAS_WANG_HASH_6
  ip = (ip^0xdeadbeef) + (ip<<4);
  ip = ip ^ (ip>>10);
  ip = ip + (ip<<7);
  ip = ip ^ (ip>>13);
#endif
#ifdef THOMAS_WANG_HASH_7
  ip = ip ^ (ip>>4);
  ip = (ip^0xdeadbeef) + (ip<<5);
  ip = ip ^ (ip>>11);
#endif
#ifdef THOMAS_WANG_NEW_HASH
  ip ^= (ip >> 20) ^ (ip >> 12);
  ip = ip ^ (ip >> 7) ^ (ip >> 4);
#endif
#ifdef THOMAS_WANG_HASH_HALF_AVALANCHE
  ip = (ip+0x479ab41d) + (ip<<8);
  ip = (ip^0xe4aa10ce) ^ (ip>>5);
  ip = (ip+0x9942f0a6) - (ip<<14);
  ip = (ip^0x5aedd67d) ^ (ip>>3);
  ip = (ip+0x17bea992) + (ip<<7);
#endif
#ifdef THOMAS_WANG_HASH_FULL_AVALANCHE
  ip = (ip+0x7ed55d16) + (ip<<12);
  ip = (ip^0xc761c23c) ^ (ip>>19);
  ip = (ip+0x165667b1) + (ip<<5);
  ip = (ip+0xd3a2646c) ^ (ip<<9);
  ip = (ip+0xfd7046c5) + (ip<<3);
  ip = (ip^0xb55a4f09) ^ (ip>>16);
#endif
#ifdef THOMAS_WANG_HASH_INT_1
  ip -= (ip<<6);
  ip ^= (ip>>17);
  ip -= (ip<<9);
  ip ^= (ip<<4);
  ip -= (ip<<3);
  ip ^= (ip<<10);
  ip ^= (ip>>15);
#endif
#ifdef THOMAS_WANG_HASH_INT_2
  ip += ~(ip<<15);
  ip ^=  (ip>>10);
  ip +=  (ip<<3);
  ip ^=  (ip>>6);
  ip += ~(ip<<11);
  ip ^=  (ip>>16);
#endif
#ifdef ENTANGLING_HASH
  ip = ip ^ (ip >> 2) ^ (ip >> 5);
#endif
#ifdef FOLD_HASH
  uint64_t hash = 0;
  while(ip) {hash ^= (ip & IP_MASK); ip >>= SIZE_IP_MASK;}
  ip = hash;
#endif
  return ip; // No IP hash
}

/******************************************************************************/
/*                        Cache Functions                                     */
/******************************************************************************/
void CACHE::prefetcher_initialize() 
{
  // Calculate latency table size
  uint64_t latency_table_size = get_mshr_size();
  for (auto const &i : get_rq_size()) latency_table_size += i;
  for (auto const &i : get_wq_size()) latency_table_size += i;
  for (auto const &i : get_pq_size()) latency_table_size += i;

  // New structures
  picturePF foo;
  foo.latencyt = new LatencyTable(latency_table_size);
  foo.scache = new ShadowCache(this->NUM_SET, this->NUM_WAY);
  foo.historyt = new HistoryTable();
  foo.berti = new Berti(BERTI_TABLE_DELTA_SIZE);
  bigPicture.push_back(foo);

  std::cout << "Berti_tri Prefetcher" << std::endl;

# ifdef NO_CROSS_PAGE
  std::cout << "No Crossing Page" << std::endl;
# endif
#ifdef HASH_ORIGINAL
  std::cout << "BERTI HASH ORIGINAL" << std::endl;
#endif
#ifdef THOMAS_WANG_HASH_1
  std::cout << "BERTI HASH 1" << std::endl;
#endif
#ifdef THOMAS_WANG_HASH_2
  std::cout << "BERTI HASH 2" << std::endl;
#endif
#ifdef THOMAS_WANG_HASH_3
  std::cout << "BERTI HASH 3" << std::endl;
#endif
#ifdef THOMAS_WANG_HASH_4
  std::cout << "BERTI HASH 4" << std::endl;
#endif
#ifdef THOMAS_WANG_HASH_5
  std::cout << "BERTI HASH 5" << std::endl;
#endif
#ifdef THOMAS_WANG_HASH_6
  std::cout << "BERTI HASH 6" << std::endl;
#endif
#ifdef THOMAS_WANG_HASH_7
  std::cout << "BERTI HASH 7" << std::endl;
#endif
#ifdef THOMAS_WANG_NEW_HASH
  std::cout << "BERTI HASH NEW" << std::endl;
#endif
#ifdef THOMAS_WANG_HASH_HALF_AVALANCHE
  std::cout << "BERTI HASH HALF AVALANCHE" << std::endl;
#endif
#ifdef THOMAS_WANG_HASH_FULL_AVALANCHE
  std::cout << "BERTI HASH FULL AVALANCHE" << std::endl;
#endif
#ifdef THOMAS_WANG_HASH_INT_1
  std::cout << "BERTI HASH INT 1" << std::endl;
#endif
#ifdef THOMAS_WANG_HASH_INT_2
  std::cout << "BERTI HASH INT 2" << std::endl;
#endif
#ifdef ENTANGLING_HASH
  std::cout << "BERTI HASH ENTANGLING" << std::endl;
#endif
#ifdef FOLD_HASH
  std::cout << "BERTI HASH FOLD" << std::endl;
#endif
  std::cout << "BERTI IP MASK " << std::hex << IP_MASK << std::dec << std::endl;

#if DRAM_PREFETCH_ENABLED
  std::cout << "DRAM prefetching enabled, threshold=" << DRAM_PREFETCH_THRESHOLD;
#if DRAM_PREFETCH_SORT
  std::cout << ", sorted by confidence";
#else
  std::cout << ", unsorted";
#endif
  std::cout << std::endl;
#endif
}

void CACHE::prefetcher_cycle_operate()
{
  // We select the structures for every cpu
  latencyt = bigPicture[cpu].latencyt;
  scache = bigPicture[cpu].scache;
  historyt = bigPicture[cpu].historyt;
  berti = bigPicture[cpu].berti;

  // Issue delayed prefetches
  berti->issue_delayed_prefetches(current_cycle, this);
}

uint32_t CACHE::prefetcher_cache_operate(uint64_t addr, uint64_t ip, 
                                         uint8_t cache_hit, bool useful_prefetch, 
                                         uint8_t type, uint32_t metadata_in)
{
  // We select the structures for every cpu
  latencyt = bigPicture[cpu].latencyt;
  scache = bigPicture[cpu].scache;
  historyt = bigPicture[cpu].historyt;
  berti = bigPicture[cpu].berti;

  uint64_t line_addr = (addr >> LOG2_BLOCK_SIZE); // Line addr
   
  if (line_addr == 0) return metadata_in;
  
  if constexpr (champsim::debug_print) 
  {
    std::cout << "[BERTI] operate";
    std::cout << " ip: " << std::hex << ip;
    std::cout << " full_address: " << addr;
    std::cout << " line_address: " << line_addr << std::dec << std::endl ;
  }

  uint64_t ip_hash = berti->ip_hash(ip) & IP_MASK;

  if (!cache_hit) // This is a miss
  {
    if constexpr (champsim::debug_print) 
      std::cout << "[BERTI] operate cache miss" << std::endl;

    latencyt->add(line_addr, ip_hash, false, current_cycle); // Add @ to latency
    historyt->add(ip_hash, line_addr, current_cycle); // Add to the table
  } else if (cache_hit && scache->is_pf(line_addr)) // Hit bc prefetch
  {
    if constexpr (champsim::debug_print)
      std::cout << "[BERTI] operate cache hit because of pf" << std::endl;

    scache->set_pf(line_addr, false);

    uint64_t latency = scache->get_latency(line_addr); // Get latency

    if (latency > LAT_MASK) latency = 0;

    berti->find_and_update(latency, ip_hash, current_cycle & TIME_MASK, line_addr);
    historyt->add(ip_hash, line_addr, current_cycle & TIME_MASK);
  } else
  {
    if constexpr (champsim::debug_print) 
      std::cout << "[BERTI] operate cache hit" << std::endl;
  }

  std::vector<delta_t> deltas(BERTI_TABLE_DELTA_SIZE);
  berti->get(ip_hash, deltas);

  bool first_issue = true;

  // Original prefetching logic
  for (auto i: deltas)
  {
    uint64_t p_addr = (line_addr + i.delta) << LOG2_BLOCK_SIZE;
    uint64_t p_b_addr = (p_addr >> LOG2_BLOCK_SIZE);

    if (latencyt->get(p_b_addr)) continue;
    if (i.rpl == BERTI_R) break; // Don't return yet - we'll try DRAM warming after this loop
    if (p_addr == 0) continue;

    if ((p_addr >> LOG2_PAGE_SIZE) != (addr >> LOG2_PAGE_SIZE))
    {
      cross_page++;
# ifdef NO_CROSS_PAGE
      // We do not cross virtual page
      continue;
# endif
    } else no_cross_page++;

    float mshr_load = get_mshr_occupancy_ratio() * 100;

    bool fill_this_level = (i.rpl == BERTI_L1) && (mshr_load < MSHR_LIMIT);

    if (i.rpl == BERTI_L1 && mshr_load >= MSHR_LIMIT) pf_to_l2_bc_mshr++; 
    if (fill_this_level) pf_to_l1++;
    else pf_to_l2++;

    if (prefetch_line(p_addr, fill_this_level, metadata_in))
    {
      ++average_issued;
      if (first_issue)
      {
        first_issue = false;
        ++average_num;
      }

      if constexpr (champsim::debug_print)
      {
        std::cout << "[BERTI] operate prefetch delta: " << i.delta;
        std::cout << " p_addr: " << std::hex << p_addr << std::dec;
        std::cout << " this_level: " << +fill_this_level << std::endl;
      }

      if (fill_this_level)
      {
        if (!scache->get(p_b_addr))
        {
          latencyt->add(p_b_addr, ip_hash, true, current_cycle);
        }
      }
    }
  }

  // New DRAM warming logic for low-confidence deltas
  if (DRAM_PREFETCH_ENABLED && CACHE::llc_static) {
    std::vector<delta_t> warming_deltas;
    if (berti->get_dram_prefetch_candidates(ip_hash, warming_deltas, DRAM_PREFETCH_THRESHOLD)) {
      for (auto& i : warming_deltas) {
        uint64_t p_addr = (line_addr + i.delta) << LOG2_BLOCK_SIZE;
        uint64_t p_b_addr = (p_addr >> LOG2_BLOCK_SIZE);

        // Skip if already being tracked or if address is invalid
        if (latencyt->get(p_b_addr) || p_addr == 0)
          continue;

        // Add to buffer instead of issuing directly
        berti->add_to_low_confidence_buffer(p_addr, metadata_in, current_cycle, i.conf);
      }
    }
  }

  return metadata_in;
}

uint32_t CACHE::prefetcher_cache_fill(uint64_t addr, uint32_t set, uint32_t way, 
                                      uint8_t prefetch, uint64_t evicted_addr,
                                      uint32_t metadata_in)
{
  // We select the structures for every cpu
  latencyt = bigPicture[cpu].latencyt;
  scache = bigPicture[cpu].scache;
  historyt = bigPicture[cpu].historyt;
  berti = bigPicture[cpu].berti;

  uint64_t line_addr = (addr >> LOG2_BLOCK_SIZE); // Line addr
  uint64_t tag     = latencyt->get_tag(line_addr);
  uint64_t cycle   = latencyt->del(line_addr) & TIME_MASK;
  uint64_t latency = 0;

  if constexpr (champsim::debug_print)
  {
    std::cout << "[BERTI] fill addr: " << std::hex << line_addr;
    std::cout << " event_cycle: " << cycle;
    std::cout << " prefetch: " << +prefetch << std::endl;
    std::cout << " latency: " << latency << std::endl;
  }

  if (cycle != 0 && ((current_cycle & TIME_MASK) > cycle))
    latency = (current_cycle & TIME_MASK) - cycle;

  if (latency > LAT_MASK)
  {
    latency = 0;
    cant_track_latency++;
  } else
  {
    if (latency != 0)
    {
      // Calculate average latency
      if (average_latency.num == 0) average_latency.average = (float) latency;
      else
      {
        average_latency.average = average_latency.average + 
          ((((float) latency) - average_latency.average) / average_latency.num);
      }
      average_latency.num++;
    }
  }

  // Add to the shadow cache
  scache->add(set, way, line_addr, prefetch, latency);

  if (latency != 0 && !prefetch)
  {
    berti->find_and_update(latency, tag, cycle, line_addr);
  }
  return metadata_in;
}

void CACHE::prefetcher_final_stats()
{
  std::cout << "BERTI_TRI " << "TO_L1: " << pf_to_l1 << " TO_L2: " << pf_to_l2;
  std::cout << " TO_L2_BC_MSHR: " << pf_to_l2_bc_mshr << std::endl;

  std::cout << "BERTI AVG_LAT: ";
  std::cout << average_latency.average << " NUM_TRACK_LATENCY: ";
  std::cout << average_latency.num << " NUM_CANT_TRACK_LATENCY: ";
  std::cout << cant_track_latency << std::endl;

  std::cout << "BERTI CROSS_PAGE " << cross_page;
  std::cout << " NO_CROSS_PAGE: " << no_cross_page << std::endl;

  std::cout << "BERTI_TRI";
  std::cout << " FOUND_BERTI: " << found_berti;
  std::cout << " NO_FOUND_BERTI: " << no_found_berti << std::endl;

  std::cout << "BERTI_TRI";
  std::cout << " AVERAGE_ISSUED: " << ((1.0*average_issued)/average_num);
  std::cout << std::endl;

  // Report DRAM warming stats
  std::cout << "BERTI_TRI DRAM_PREFETCH_ISSUED: " << dram_prefetch_issued;
#if DRAM_PREFETCH_ENABLED
  std::cout << " (enabled, threshold=" << DRAM_PREFETCH_THRESHOLD << ")";
#else
  std::cout << " (disabled)";
#endif

#ifdef DRAM_PREFETCH_SORT
  std::cout << " | ordering enabled";
#endif
  std::cout << std::endl;

  std::cout << "BERTI_TRI DELAYED_PREFETCH_STATS:";
  std::cout << " total_issued=" << dram_prefetch_issued;
  std::cout << " late_removed=" << berti->late_removed;
  std::cout << " times_throttled=" << berti->throttled_prefetches;
  std::cout << " avg_delay=";
  if (average_latency.num > 0) {
    std::cout << (average_latency.average * LOW_CONF_AVG_DELAY_FACTOR);
  } else {
    std::cout << "unknown";
  }
  std::cout << " avg_delay_factor=" << LOW_CONF_AVG_DELAY_FACTOR;
  std::cout << " timing_window=" << LOW_CONF_LATE_SLACK << "cycles";
  std::cout << std::endl;
}
