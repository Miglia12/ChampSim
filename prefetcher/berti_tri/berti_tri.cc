#include "berti_tri.h"

/*
 * Berti-Tri: An extension of the Berti prefetcher with DRAM row warming
 * Based on Berti prefetcher from:
 * 55th ACM/IEEE International Conference on Microarchitecture (MICRO 2022)
 */

using namespace berti_tri_params;

// Helper function to get the current cycle
inline uint64_t berti_tri::get_current_cycle() { return static_cast<uint64_t>(intern_->current_time.time_since_epoch() / intern_->clock_period); }

/******************************************************************************/
/*                      Latency table functions                               */
/******************************************************************************/

berti_tri::LatencyTable::LatencyTable(const int size_param) : size(size_param) { latencyt = new latency_table[size_param]; }

berti_tri::LatencyTable::~LatencyTable() { delete[] latencyt; }

uint8_t berti_tri::LatencyTable::add(champsim::block_number addr, uint64_t tag, bool pf, uint64_t cycle)
{
  /*
   * Save if possible the new miss into the pqmshr (latency) table
   *
   * Parameters:
   *  - addr: address without cache offset
   *  - access: is the entry accessed by a demand request
   *  - cycle: time to use in the latency table
   *
   * Return: pf
   */

  if constexpr (champsim::debug_print) {
    std::cout << "[BERTI_LATENCY_TABLE] " << __func__;
    std::cout << " addr: " << addr << " tag: " << std::hex << tag;
    std::cout << " prefetch: " << std::dec << +pf << " cycle: " << cycle;
  }

  latency_table* free = nullptr;

  for (int i = 0; i < size; i++) {
    // Search if the addr already exists. If it exist we does not have
    // to do nothing more
    if (latencyt[i].addr == addr) {
      if constexpr (champsim::debug_print) {
        std::cout << " line already found; find_tag: " << latencyt[i].tag;
        std::cout << " find_pf: " << +latencyt[i].pf << std::endl;
      }
      // latencyt[i].time = cycle;
      latencyt[i].pf = pf;
      latencyt[i].tag = tag;
      return latencyt[i].pf;
    }

    // We discover a free space into the latency table, save it for later
    if (latencyt[i].tag == 0)
      free = &latencyt[i];
  }

  if (free == nullptr)
    assert(0 && "No free space latency table");

  // We save the new entry into the latency table
  free->addr = addr;
  free->time = cycle;
  free->tag = tag;
  free->pf = pf;

  if constexpr (champsim::debug_print)
    std::cout << " new entry" << std::endl;
  return free->pf;
}

uint64_t berti_tri::LatencyTable::del(champsim::block_number addr)
{
  /*
   * Remove the address from the latency table
   *
   * Parameters:
   *  - addr: address without cache offset
   *
   *  Return: the latency of the address
   */

  if constexpr (champsim::debug_print) {
    std::cout << "[BERTI_LATENCY_TABLE] " << __func__;
    std::cout << " addr: " << addr;
  }

  for (int i = 0; i < size; i++) {
    // Line already in the table
    if (latencyt[i].addr == addr) {
      // Calculate latency
      uint64_t time = latencyt[i].time;

      if constexpr (champsim::debug_print) {
        std::cout << " tag: " << latencyt[i].tag;
        std::cout << " prefetch: " << std::dec << +latencyt[i].pf;
        std::cout << " cycle: " << latencyt[i].time << std::endl;
      }

      champsim::block_number empty_addr{};
      latencyt[i].addr = empty_addr; // Free the entry
      latencyt[i].tag = 0;           // Free the entry
      latencyt[i].time = 0;          // Free the entry
      latencyt[i].pf = 0;            // Free the entry

      // Return the latency
      return time;
    }
  }

  // We should always track the misses
  if constexpr (champsim::debug_print)
    std::cout << " TRANSLATION" << std::endl;
  return 0;
}

uint64_t berti_tri::LatencyTable::get(champsim::block_number addr)
{
  /*
   * Return time or 0 if the addr is or is not in the pqmshr (latency) table
   *
   * Parameters:
   *  - addr: address without cache offset
   *
   * Return: time if the line is in the latency table, otherwise 0
   */

  if constexpr (champsim::debug_print) {
    std::cout << "[BERTI_LATENCY_TABLE] " << __func__;
    std::cout << " addr: " << addr;
  }

  for (int i = 0; i < size; i++) {
    // Search if the addr already exists
    if (latencyt[i].addr == addr) {
      if constexpr (champsim::debug_print) {
        std::cout << " time: " << latencyt[i].time << std::endl;
      }
      return latencyt[i].time;
    }
  }

  if constexpr (champsim::debug_print)
    std::cout << " NOT FOUND" << std::endl;
  return 0;
}

uint64_t berti_tri::LatencyTable::get_tag(champsim::block_number addr)
{
  /*
   * Return IP-Tag or 0 if the addr is or is not in the pqmshr (latency) table
   *
   * Parameters:
   *  - addr: address without cache offset
   *
   * Return: ip-tag if the line is in the latency table, otherwise 0
   */

  if constexpr (champsim::debug_print) {
    std::cout << "[BERTI_LATENCY_TABLE] " << __func__;
    std::cout << " addr: " << addr;
  }

  for (int i = 0; i < size; i++) {
    if (latencyt[i].addr == addr && latencyt[i].tag) { // This is the address
      if constexpr (champsim::debug_print) {
        std::cout << " tag: " << latencyt[i].tag << std::endl;
      }
      return latencyt[i].tag;
    }
  }

  if constexpr (champsim::debug_print)
    std::cout << " NOT_FOUND" << std::endl;
  return 0;
}

/******************************************************************************/
/*                       Shadow Cache functions                               */
/******************************************************************************/

berti_tri::ShadowCache::ShadowCache(const int sets_param, const int ways_param)
{
  scache = new shadow_cache*[sets_param];
  for (int i = 0; i < sets_param; i++)
    scache[i] = new shadow_cache[ways_param];

  this->sets = sets_param;
  this->ways = ways_param;
}

berti_tri::ShadowCache::~ShadowCache()
{
  for (int i = 0; i < sets; i++)
    delete[] scache[i];
  delete[] scache;
}

bool berti_tri::ShadowCache::add(uint32_t set, uint32_t way, champsim::block_number addr, bool pf, uint64_t lat)
{
  /*
   * Add block to shadow cache
   *
   * Parameters:
   *      - set: cache set
   *      - way: cache way
   *      - addr: cache block v_addr
   *      - pf: is this a prefetch
   *      - lat: latency value
   */

  if constexpr (champsim::debug_print) {
    std::cout << "[BERTI_SHADOW_CACHE] " << __func__;
    std::cout << " set: " << set << " way: " << way;
    std::cout << " addr: " << addr;
    std::cout << " pf: " << +pf;
    std::cout << " latency: " << lat << std::endl;
  }

  scache[set][way].addr = addr;
  scache[set][way].pf = pf;
  scache[set][way].lat = lat;
  return scache[set][way].pf;
}

bool berti_tri::ShadowCache::get(champsim::block_number addr)
{
  /*
   * Parameters:
   *      - addr: cache block v_addr
   *
   * Return: true if the addr is in the l1d cache, false otherwise
   */

  if constexpr (champsim::debug_print) {
    std::cout << "[BERTI_SHADOW_CACHE] " << __func__;
    std::cout << " addr: " << addr << std::endl;
  }

  for (int i = 0; i < sets; i++) {
    for (int ii = 0; ii < ways; ii++) {
      if (scache[i][ii].addr == addr) {
        if constexpr (champsim::debug_print) {
          std::cout << " set: " << i << " way: " << i << std::endl;
        }
        return true;
      }
    }
  }

  return false;
}

void berti_tri::ShadowCache::set_pf(champsim::block_number addr, bool pf)
{
  /*
   * Parameters:
   *      - addr: cache block v_addr
   *
   * Return: change value of pf field
   */
  if constexpr (champsim::debug_print) {
    std::cout << "[BERTI_SHADOW_CACHE] " << __func__;
    std::cout << " addr: " << addr;
  }

  for (int i = 0; i < sets; i++) {
    for (int ii = 0; ii < ways; ii++) {
      if (scache[i][ii].addr == addr) {
        if constexpr (champsim::debug_print) {
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

bool berti_tri::ShadowCache::is_pf(champsim::block_number addr)
{
  /*
   * Parameters:
   *      - addr: cache block v_addr
   *
   * Return: True if the saved one is a prefetch
   */

  if constexpr (champsim::debug_print) {
    std::cout << "[BERTI_SHADOW_CACHE] " << __func__;
    std::cout << " addr: " << addr;
  }

  for (int i = 0; i < sets; i++) {
    for (int ii = 0; ii < ways; ii++) {
      if (scache[i][ii].addr == addr) {
        if constexpr (champsim::debug_print) {
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

uint64_t berti_tri::ShadowCache::get_latency(champsim::block_number addr)
{
  /*
   * Init shadow cache
   *
   * Parameters:
   *      - addr: cache block v_addr
   *
   * Return: the saved latency
   */
  if constexpr (champsim::debug_print) {
    std::cout << "[BERTI_SHADOW_CACHE] " << __func__;
    std::cout << " addr: " << addr;
  }

  for (int i = 0; i < sets; i++) {
    for (int ii = 0; ii < ways; ii++) {
      if (scache[i][ii].addr == addr) {
        if constexpr (champsim::debug_print) {
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
/*                       History Table functions                              */
/******************************************************************************/
berti_tri::HistoryTable::HistoryTable()
{
  history_pointers = new history_table*[sets];
  historyt = new history_table*[sets];

  for (int i = 0; i < sets; i++)
    historyt[i] = new history_table[ways];
  for (int i = 0; i < sets; i++)
    history_pointers[i] = historyt[i];
}

berti_tri::HistoryTable::~HistoryTable()
{
  for (int i = 0; i < sets; i++)
    delete[] historyt[i];
  delete[] historyt;

  delete[] history_pointers;
}

void berti_tri::HistoryTable::add(uint64_t tag, champsim::block_number addr, uint64_t cycle)
{
  /*
   * Save the new information into the history table
   *
   * Parameters:
   *  - tag: PC tag
   *  - addr: block address access
   */
  uint16_t set = tag & TABLE_SET_MASK;
  // If the latest entry is the same, we do not add it
  if (history_pointers[set] == &historyt[set][ways - 1]) {

    uint64_t masked_addr = addr.to<uint64_t>() & ADDR_MASK;
    uint64_t history_addr = historyt[set][0].addr.to<uint64_t>() & ADDR_MASK;

    if (masked_addr == history_addr)
      return;
  } else {
    // Extract addr masked with ADDR_MASK
    uint64_t masked_addr = addr.to<uint64_t>() & ADDR_MASK;

    // Get the masked addr from the history table
    uint64_t history_addr = (history_pointers[set] - 1)->addr.to<uint64_t>() & ADDR_MASK;

    if (masked_addr == history_addr)
      return;
  }

  // Save new element into the history table
  history_pointers[set]->tag = tag;
  history_pointers[set]->time = cycle & TIME_MASK;
  history_pointers[set]->addr = addr;

  if constexpr (champsim::debug_print) {
    std::cout << "[BERTI_HISTORY_TABLE] " << __func__;
    std::cout << " tag: " << std::hex << tag << " line_addr: " << addr << std::dec;
    std::cout << " cycle: " << cycle << " set: " << set << std::endl;
  }

  if (history_pointers[set] == &historyt[set][ways - 1]) {
    history_pointers[set] = &historyt[set][0]; // End the cycle
  } else
    history_pointers[set]++; // Pointer to the next (oldest) entry
}

uint16_t berti_tri::HistoryTable::get_aux(uint32_t latency, uint64_t tag, champsim::block_number act_addr, uint64_t* tags, champsim::block_number* addr,
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

  if constexpr (champsim::debug_print) {
    std::cout << "[BERTI_HISTORY_TABLE] " << __func__;
    std::cout << " tag: " << std::hex << tag << " addr: " << act_addr << std::dec;
    std::cout << " cycle: " << cycle << " set: " << set << std::endl;
  }

  // This is the begin of the simulation
  if (cycle < latency)
    return num_on_time;

  // The IPs that is launch in this cycle will be able to launch this prefetch
  cycle -= latency;

  // Pointer to guide
  history_table* pointer = history_pointers[set];

  do {
    // Look for the IPs that can launch this prefetch
    if (pointer->tag == tag && pointer->time <= cycle) {
      // Extract addr masked with ADDR_MASK
      uint64_t masked_act_addr = act_addr.to<uint64_t>() & ADDR_MASK;

      // Get the masked addr from the history table
      uint64_t history_addr = pointer->addr.to<uint64_t>() & ADDR_MASK;

      // Test that addr is not duplicated
      if (masked_act_addr == history_addr)
        return num_on_time;

      // This IP can launch the prefetch
      tags[num_on_time] = pointer->tag;
      addr[num_on_time] = pointer->addr;
      num_on_time++;
    }

    if (pointer == historyt[set]) {
      // We get at the end of the history, we start again
      pointer = &historyt[set][ways - 1];
    } else
      pointer--;
  } while (pointer != history_pointers[set]);

  return num_on_time;
}

uint16_t berti_tri::HistoryTable::get(uint32_t latency, uint64_t tag, champsim::block_number act_addr, uint64_t* tags, champsim::block_number* addr,
                                      uint64_t cycle)
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

  uint16_t num_on_time = get_aux(latency, tag, act_addr, tags, addr, cycle & TIME_MASK);

  // We found on-time prefetchs
  return num_on_time;
}

/******************************************************************************/
/*                        Berti table functions                               */
/******************************************************************************/
void berti_tri::increase_conf_tag(uint64_t tag)
{
  /*
   * Increase the global confidence of the deltas associated to the tag
   *
   * Parameters:
   *  tag : tag to find
   */
  if constexpr (champsim::debug_print)
    std::cout << "[BERTI_BERTI] " << __func__ << " tag: " << std::hex << tag << std::dec;

  if (bertit.find(tag) == bertit.end()) {
    // Tag not found
    if constexpr (champsim::debug_print)
      std::cout << " TAG NOT FOUND" << std::endl;

    return;
  }

  // Get the entries and the deltas
  bertit[tag]->conf += CONFIDENCE_INC;

  if constexpr (champsim::debug_print)
    std::cout << " global_conf: " << bertit[tag]->conf;

  if (bertit[tag]->conf == CONFIDENCE_MAX) {
    // Max confidence achieve
    for (auto& i : bertit[tag]->deltas) {
      // Set bits to prefetch level
      if (i.conf > CONFIDENCE_L1)
        i.rpl = BERTI_L1;
      else if (i.conf > CONFIDENCE_L2)
        i.rpl = BERTI_L2;
      else if (i.conf > CONFIDENCE_L2R)
        i.rpl = BERTI_L2R;
      else
        i.rpl = BERTI_R;

      if constexpr (champsim::debug_print) {
        std::cout << "Delta: " << i.delta;
        std::cout << " Conf: " << i.conf << " Level: " << +i.rpl;
        std::cout << "|";
      }

      i.conf = 0; // Reset confidence
    }

    bertit[tag]->conf = 0; // Reset global confidence
  }

  if constexpr (champsim::debug_print)
    std::cout << std::endl;
}

void berti_tri::add(uint64_t tag, int64_t delta)
{
  /*
   * Save the new information into the history table
   *
   * Parameters:
   *  - tag: PC tag
   *  - delta: stride value
   */
  if constexpr (champsim::debug_print) {
    std::cout << "[BERTI_BERTI] " << __func__;
    std::cout << " tag: " << std::hex << tag << std::dec;
    std::cout << " delta: " << delta;
  }

  auto add_delta = [](auto new_delta, auto entry) {
    // Lambda function to add a new element
    Delta new_delta_entry;
    new_delta_entry.delta = new_delta;
    new_delta_entry.conf = CONFIDENCE_INIT;
    new_delta_entry.rpl = BERTI_R;
    auto it = std::find_if(std::begin(entry->deltas), std::end(entry->deltas), [](const auto i) { return (i.delta == 0); });
    assert(it != std::end(entry->deltas));
    *it = new_delta_entry;
  };

  if (bertit.find(tag) == bertit.end()) {
    if constexpr (champsim::debug_print)
      std::cout << " allocating a new entry;";

    // We are not tracking this tag
    if (bertit_queue.size() > BERTI_TABLE_SIZE) {
      // FIFO replacent algorithm
      uint64_t key = bertit_queue.front();
      berti* entry = bertit[key];

      if constexpr (champsim::debug_print)
        std::cout << " removing tag: " << std::hex << key << std::dec << ";";

      delete entry; // Free previous entry

      bertit.erase(bertit_queue.front());
      bertit_queue.pop();
    }

    bertit_queue.push(tag); // Add new tag
    assert((bertit.size() <= BERTI_TABLE_SIZE) && "Tracking too much tags");

    // Confidence IP
    berti* entry = new berti;
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
  berti* entry = bertit[tag];

  for (auto& i : entry->deltas) {
    if (i.delta == delta) {
      // We already track the delta
      i.conf += CONFIDENCE_INC;

      if (i.conf > CONFIDENCE_MAX)
        i.conf = CONFIDENCE_MAX;

      if constexpr (champsim::debug_print)
        std::cout << " confidence: " << i.conf << std::endl;

      return;
    }
  }

  // We have space to add a new entry
  auto ssize = std::count_if(std::begin(entry->deltas), std::end(entry->deltas), [](const auto i) { return i.delta != 0; });

  if (ssize < size) {
    add_delta(delta, entry);
    assert((std::size(entry->deltas) <= size) && "I remember too much deltas");
    return;
  }

  // We find the delta with less confidence
  std::sort(std::begin(entry->deltas), std::end(entry->deltas), compare_rpl);
  if (entry->deltas.front().rpl == BERTI_R || entry->deltas.front().rpl == BERTI_L2R) {
    if constexpr (champsim::debug_print)
      std::cout << " replaced_delta: " << entry->deltas.front().delta << std::endl;
    entry->deltas.front().delta = delta;
    entry->deltas.front().conf = CONFIDENCE_INIT;
    entry->deltas.front().rpl = BERTI_R;
  }
}

uint8_t berti_tri::get(uint64_t tag, std::vector<Delta>& res)
{
  /*
   * Get deltas associated with a tag
   *
   * Parameters:
   *  - tag: PC tag
   *
   * Return: the stride to prefetch
   */
  if constexpr (champsim::debug_print) {
    std::cout << "[BERTI_BERTI] " << __func__ << " tag: " << std::hex << tag;
    std::cout << std::dec;
  }

  if (!bertit.count(tag)) {
    if constexpr (champsim::debug_print)
      std::cout << " TAG NOT FOUND" << std::endl;
    return 0;
  }

  if constexpr (champsim::debug_print)
    std::cout << std::endl;

  // We found the tag
  berti* entry = bertit[tag];

  for (auto& i : entry->deltas)
    if (i.delta != 0 && i.rpl != BERTI_R)
      res.push_back(i);

  if (res.empty() && entry->conf >= LAUNCH_MIDDLE_CONF) {
    // We do not find any delta, so we will try to launch with small confidence
    for (auto& i : entry->deltas) {
      if (i.delta != 0) {
        Delta new_delta;
        new_delta.delta = i.delta;
        if (i.conf > CONFIDENCE_MIDDLE_L1)
          new_delta.rpl = BERTI_L1;
        else if (i.conf > CONFIDENCE_MIDDLE_L2)
          new_delta.rpl = BERTI_L2;
        else
          continue;
        res.push_back(new_delta);
      }
    }
  }

  // Sort the entries
  std::sort(std::begin(res), std::end(res), compare_greater_delta);
  return 1;
}

void berti_tri::find_and_update(uint64_t latency, uint64_t tag, uint64_t cycle, champsim::block_number line_addr)
{
  // We were tracking this miss
  uint64_t tags[HISTORY_TABLE_WAYS];
  champsim::block_number addr[HISTORY_TABLE_WAYS];
  uint16_t num_on_time = 0;

  // Get the IPs that can launch a prefetch
  num_on_time = historyt->get(latency, tag, line_addr, tags, addr, cycle);

  for (uint32_t i = 0; i < num_on_time; i++) {
    // Increase conf tag
    if (i == 0)
      increase_conf_tag(tag);

    // Add information into berti table
    int64_t stride;

    // Usually applications go from lower to higher memory position.
    // The operation order is important (mainly because we allow
    // negative strides)
    stride = champsim::offset(addr[i], line_addr);

    if ((std::abs(stride) < (1 << DELTA_MASK)))
      add(tags[i], stride);
  }
}

bool berti_tri::compare_rpl(Delta a, Delta b)
{
  if (a.rpl == BERTI_R && b.rpl != BERTI_R)
    return 1;
  else if (b.rpl == BERTI_R && a.rpl != BERTI_R)
    return 0;
  else if (a.rpl == BERTI_L2R && b.rpl != BERTI_L2R)
    return 1;
  else if (b.rpl == BERTI_L2R && a.rpl != BERTI_L2R)
    return 0;
  else {
    if (a.conf < b.conf)
      return 1;
    else
      return 0;
  }
}

bool berti_tri::compare_greater_delta(Delta a, Delta b)
{
  // Sorted stride when the confidence is full
  if (a.rpl == BERTI_L1 && b.rpl != BERTI_L1)
    return 1;
  else if (a.rpl != BERTI_L1 && b.rpl == BERTI_L1)
    return 0;
  else {
    if (a.rpl == BERTI_L2 && b.rpl != BERTI_L2)
      return 1;
    else if (a.rpl != BERTI_L2 && b.rpl == BERTI_L2)
      return 0;
    else {
      if (a.rpl == BERTI_L2R && b.rpl != BERTI_L2R)
        return 1;
      if (a.rpl != BERTI_L2R && b.rpl == BERTI_L2R)
        return 0;
      else {
        if (std::abs(a.delta) < std::abs(b.delta))
          return 1;
        return 0;
      }
    }
  }
}

uint64_t berti_tri::ip_hash(uint64_t ip)
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
  ip = (ip + 0x7ed55d16) + (ip << 12);
  ip = (ip ^ 0xc761c23c) ^ (ip >> 19);
  ip = (ip + 0x165667b1) + (ip << 5);
  ip = (ip + 0xd3a2646c) ^ (ip << 9);
  ip = (ip + 0xfd7046c5) + (ip << 3);
  ip = (ip ^ 0xb55a4f09) ^ (ip >> 16);
#endif
#ifdef THOMAS_WANG_HASH_3
  ip -= (ip << 6);
  ip ^= (ip >> 17);
  ip -= (ip << 9);
  ip ^= (ip << 4);
  ip -= (ip << 3);
  ip ^= (ip << 10);
  ip ^= (ip >> 15);
#endif
#ifdef THOMAS_WANG_HASH_4
  ip += ~(ip << 15);
  ip ^= (ip >> 10);
  ip += (ip << 3);
  ip ^= (ip >> 6);
  ip += ~(ip << 11);
  ip ^= (ip >> 16);
#endif
#ifdef THOMAS_WANG_HASH_5
  ip = (ip + 0x479ab41d) + (ip << 8);
  ip = (ip ^ 0xe4aa10ce) ^ (ip >> 5);
  ip = (ip + 0x9942f0a6) - (ip << 14);
  ip = (ip ^ 0x5aedd67d) ^ (ip >> 3);
  ip = (ip + 0x17bea992) + (ip << 7);
#endif
#ifdef THOMAS_WANG_HASH_6
  ip = (ip ^ 0xdeadbeef) + (ip << 4);
  ip = ip ^ (ip >> 10);
  ip = ip + (ip << 7);
  ip = ip ^ (ip >> 13);
#endif
#ifdef THOMAS_WANG_HASH_7
  ip = ip ^ (ip >> 4);
  ip = (ip ^ 0xdeadbeef) + (ip << 5);
  ip = ip ^ (ip >> 11);
#endif
#ifdef THOMAS_WANG_NEW_HASH
  ip ^= (ip >> 20) ^ (ip >> 12);
  ip = ip ^ (ip >> 7) ^ (ip >> 4);
#endif
#ifdef THOMAS_WANG_HASH_HALF_AVALANCHE
  ip = (ip + 0x479ab41d) + (ip << 8);
  ip = (ip ^ 0xe4aa10ce) ^ (ip >> 5);
  ip = (ip + 0x9942f0a6) - (ip << 14);
  ip = (ip ^ 0x5aedd67d) ^ (ip >> 3);
  ip = (ip + 0x17bea992) + (ip << 7);
#endif
#ifdef THOMAS_WANG_HASH_FULL_AVALANCHE
  ip = (ip + 0x7ed55d16) + (ip << 12);
  ip = (ip ^ 0xc761c23c) ^ (ip >> 19);
  ip = (ip + 0x165667b1) + (ip << 5);
  ip = (ip + 0xd3a2646c) ^ (ip << 9);
  ip = (ip + 0xfd7046c5) + (ip << 3);
  ip = (ip ^ 0xb55a4f09) ^ (ip >> 16);
#endif
#ifdef THOMAS_WANG_HASH_INT_1
  ip -= (ip << 6);
  ip ^= (ip >> 17);
  ip -= (ip << 9);
  ip ^= (ip << 4);
  ip -= (ip << 3);
  ip ^= (ip << 10);
  ip ^= (ip >> 15);
#endif
#ifdef THOMAS_WANG_HASH_INT_2
  ip += ~(ip << 15);
  ip ^= (ip >> 10);
  ip += (ip << 3);
  ip ^= (ip >> 6);
  ip += ~(ip << 11);
  ip ^= (ip >> 16);
#endif
#ifdef ENTANGLING_HASH
  ip = ip ^ (ip >> 2) ^ (ip >> 5);
#endif
#ifdef FOLD_HASH
  uint64_t hash = 0;
  while (ip) {
    hash ^= (ip & IP_MASK);
    ip >>= SIZE_IP_MASK;
  }
  ip = hash;
#endif
  return ip; // No IP hash
}

void berti_tri::initialize_berti_table(uint64_t table_size)
{
  bertit.clear();
  while (!bertit_queue.empty())
    bertit_queue.pop();

  this->size = table_size;
}

/******************************************************************************/
/*                   DRAM Row Opening methods                                 */
/******************************************************************************/

void berti_tri::get_dram_open_candidates(uint64_t tag, champsim::block_number base_addr, uint32_t metadata)
{
  /*
   * Similar to the original get() method, but only applies confidence-based filtering
   * and sends candidates to the scheduler
   *
   * Parameters:
   *  - tag: PC tag
   *  - base_addr: base address to prefetch from
   *  - metadata: prefetch metadata
   */

  if constexpr (champsim::debug_print) {
    std::cout << "[BERTI_TRI] " << __func__ << " tag: " << std::hex << tag;
    std::cout << " base_addr: " << base_addr << std::dec << std::endl;
  }

  if (!bertit.count(tag)) {
    if constexpr (champsim::debug_print)
      std::cout << " TAG NOT FOUND" << std::endl;
    return;
  }

  uint64_t current_cycle = get_current_cycle();

  // We found the tag
  berti* entry = bertit[tag];

  // Create a vector to store deltas for DRAM warming
  std::vector<Delta> dram_open_deltas;

  // Collect deltas with confidence within the configured range
  for (auto& i : entry->deltas) {
    if (i.delta != 0 && i.conf >= DRAM_WARM_MIN_CONF && i.conf <= DRAM_WARM_MAX_CONF) {
      Delta new_delta;
      new_delta.delta = i.delta;
      new_delta.conf = i.conf;
      new_delta.rpl = i.rpl;
      dram_open_deltas.push_back(new_delta);
    }
  }

  std::sort(std::begin(dram_open_deltas), std::end(dram_open_deltas), compare_greater_delta);

  // Now issue prefetches for each identified delta
  for (auto& i : dram_open_deltas) {
    // Calculate prefetch address
    champsim::block_number pf_block_addr = base_addr + i.delta;
    champsim::address pf_addr{pf_block_addr};

    // Skip if address is invalid
    if (pf_addr.to<uint64_t>() == 0)
      continue;

    // Skip if address is already in the latency table (already being fetched)
    if (latencyt->get(pf_block_addr))
      continue;

    // Create and add request to scheduler
    dram_open::DramRowOpenRequest row_req(pf_addr, i.conf, metadata);

    // Add to scheduler - no page boundary check
    if (row_scheduler->add_request(row_req, current_cycle)) {
      dram_warm_requests++;
    }

    if constexpr (champsim::debug_print) {
      std::cout << "[BERTI_TRI] " << __func__ << " added to scheduler: addr=" << pf_addr;
      std::cout << " delta=" << i.delta << " conf=" << i.conf << " rpl=" << +i.rpl << std::endl;
    }
  }
}

/******************************************************************************/
/*                        Module Interface Functions                          */
/******************************************************************************/

void berti_tri::prefetcher_initialize()
{
  // Initialize the regular Berti components
  initialize_berti_table(BERTI_TABLE_DELTA_SIZE);

  // Calculate latency table size
  uint64_t latency_table_size = intern_->MSHR_SIZE;
  for (auto const& i : intern_->get_rq_size())
    latency_table_size += i;
  for (auto const& i : intern_->get_wq_size())
    latency_table_size += i;
  for (auto const& i : intern_->get_pq_size())
    latency_table_size += i;

  // Initialize structures
  latencyt = std::make_unique<LatencyTable>(static_cast<int>(latency_table_size));
  scache = std::make_unique<ShadowCache>(intern_->NUM_SET, intern_->NUM_WAY);
  historyt = std::make_unique<HistoryTable>();

  // Initialize DRAM row warming scheduler
  row_scheduler = std::make_unique<dram_open::DramRowOpenScheduler>(SCHEDULER_QUEUE_SIZE, READY_THRESHOLD, SLACK);

  std::cout << "Berti-Tri Prefetcher" << std::endl;
  std::cout << "DRAM Row Warming Configuration:" << std::endl;
  std::cout << "  SCHEDULER_QUEUE_SIZE: " << SCHEDULER_QUEUE_SIZE << std::endl;
  std::cout << "  READY_THRESHOLD: " << READY_THRESHOLD << std::endl;
  std::cout << "  SLACK: " << SLACK << std::endl;
  std::cout << "  Confidence selection: [" << DRAM_WARM_MIN_CONF << " - " << DRAM_WARM_MAX_CONF << "]" << std::endl;
  std::cout << "  DRAM_WARM_BW_FRACTION: " << DRAM_WARM_BW_FRACTION << std::endl;

#ifdef NO_CROSS_PAGE
  std::cout << "No Crossing Page (for regular prefetches)" << std::endl;
#endif
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
}

void berti_tri::prefetcher_cycle_operate()
{
  uint64_t current_cycle = get_current_cycle();

  // Calculate how many prefetches we can issue this cycle
  std::size_t available_pq_slots = intern_->PQ_SIZE - intern_->get_pq_occupancy().back();
  std::size_t max_issue_per_cycle = std::max(size_t{1}, static_cast<std::size_t>(static_cast<double>(available_pq_slots) * DRAM_WARM_BW_FRACTION));

  // Create a callback for issuing DRAM row warming requests
  auto issue_callback = [this](const dram_open::DramRowOpenRequest& req) -> bool {
    // Use the special prefetch_line with open_dram_row=true for DRAM row warming
    bool success = this->prefetch_line(req.addr, false, req.metadata_in, true);
    if (success) {
      dram_row_warm_issued++;
    }
    return success;
  };

  // Process scheduler
  row_scheduler->tick(current_cycle, max_issue_per_cycle, issue_callback);
}

uint32_t berti_tri::prefetcher_cache_operate(champsim::address addr, champsim::address ip, uint8_t cache_hit, bool useful_prefetch, access_type type,
                                             uint32_t metadata_in)
{
  // Extract line address
  champsim::block_number line_addr{addr};

  if (line_addr.to<uint64_t>() == 0)
    return metadata_in;

  if constexpr (champsim::debug_print) {
    std::cout << "[BERTI_TRI] operate";
    std::cout << " ip: " << ip;
    std::cout << " full_address: " << addr;
    std::cout << " line_address: " << line_addr << std::dec << std::endl;
  }

  uint64_t hashed_ip = ip_hash(ip.to<uint64_t>()) & IP_MASK;

  if (!cache_hit) // This is a miss
  {
    if constexpr (champsim::debug_print)
      std::cout << "[BERTI_TRI] operate cache miss" << std::endl;

    uint64_t current_cycle = get_current_cycle();
    latencyt->add(line_addr, hashed_ip, false, current_cycle); // Add @ to latency
    historyt->add(hashed_ip, line_addr, current_cycle);        // Add to the table
  } else if (cache_hit && scache->is_pf(line_addr))            // Hit bc prefetch
  {
    if constexpr (champsim::debug_print)
      std::cout << "[BERTI_TRI] operate cache hit because of pf" << std::endl;

    scache->set_pf(line_addr, false);

    uint64_t latency = scache->get_latency(line_addr); // Get latency

    if (latency > LAT_MASK)
      latency = 0;

    uint64_t current_cycle = get_current_cycle();
    find_and_update(latency, hashed_ip, current_cycle & TIME_MASK, line_addr);
    historyt->add(hashed_ip, line_addr, current_cycle & TIME_MASK);
  } else {
    if constexpr (champsim::debug_print)
      std::cout << "[BERTI_TRI] operate cache hit" << std::endl;
  }

  std::vector<Delta> deltas;
  get(hashed_ip, deltas);

  // First issue high-confidence prefetches
  bool first_issue = true;
  for (auto i : deltas) {
    champsim::block_number pf_block_addr = line_addr + i.delta;
    champsim::address pf_addr{pf_block_addr};

    if (latencyt->get(pf_block_addr))
      continue;
    if (i.rpl == BERTI_R)
      return metadata_in;
    if (pf_addr.to<uint64_t>() == 0)
      continue;

    // Check if the prefetch crosses a page boundary
    champsim::page_number current_page{addr};
    champsim::page_number pf_page{pf_addr};

    if (current_page != pf_page) {
      cross_page++;
#ifdef NO_CROSS_PAGE
      // We do not cross virtual page
      continue;
#endif
    } else
      no_cross_page++;

    float mshr_load = static_cast<float>(intern_->get_mshr_occupancy_ratio() * 100);

    bool fill_this_level = (i.rpl == BERTI_L1) && (mshr_load < MSHR_LIMIT);

    if (i.rpl == BERTI_L1 && mshr_load >= MSHR_LIMIT)
      pf_to_l2_bc_mshr++;
    if (fill_this_level)
      pf_to_l1++;
    else
      pf_to_l2++;

    if (prefetch_line(pf_addr, fill_this_level, metadata_in)) {
      ++average_issued;
      if (first_issue) {
        first_issue = false;
        ++average_num;
      }

      if constexpr (champsim::debug_print) {
        std::cout << "[BERTI_TRI] operate prefetch delta: " << i.delta;
        std::cout << " p_addr: " << pf_addr;
        std::cout << " this_level: " << +fill_this_level << std::endl;
      }

      if (fill_this_level) {
        if (!scache->get(pf_block_addr)) {
          uint64_t current_cycle = get_current_cycle();
          latencyt->add(pf_block_addr, hashed_ip, true, current_cycle);
        }
      }
    }
  }

  // Collect candidates for DRAM row opening
  get_dram_open_candidates(hashed_ip, line_addr, metadata_in);

  return metadata_in;
}

uint32_t berti_tri::prefetcher_cache_fill(champsim::address addr, long set, long way, uint8_t prefetch, champsim::address evicted_addr, uint32_t metadata_in)
{
  champsim::block_number line_addr{addr};
  uint64_t tag = latencyt->get_tag(line_addr);
  uint64_t cycle = latencyt->del(line_addr) & TIME_MASK;
  uint64_t latency = 0;

  if constexpr (champsim::debug_print) {
    std::cout << "[BERTI_TRI] fill addr: " << line_addr;
    std::cout << " event_cycle: " << cycle;
    std::cout << " prefetch: " << +prefetch << std::endl;
    std::cout << " latency: " << latency << std::endl;
  }

  uint64_t current_cycle = get_current_cycle();
  if (cycle != 0 && ((current_cycle & TIME_MASK) > cycle))
    latency = (current_cycle & TIME_MASK) - cycle;

  if (latency > LAT_MASK) {
    latency = 0;
    cant_track_latency++;
  } else {
    if (latency != 0) {
      // Calculate average latency
      if (average_latency.num == 0)
        average_latency.average = (float)latency;
      else {
        average_latency.average = average_latency.average + ((((float)latency) - average_latency.average) / average_latency.num);
      }
      average_latency.num++;
    }
  }

  // Add to the shadow cache
  scache->add(static_cast<uint32_t>(set), static_cast<uint32_t>(way), line_addr, prefetch, latency);

  if (latency != 0 && !prefetch) {
    find_and_update(latency, tag, cycle, line_addr);
  }
  return metadata_in;
}

void berti_tri::prefetcher_final_stats()
{
  std::cout << "BERTI_TRI " << "TO_L1: " << pf_to_l1 << " TO_L2: " << pf_to_l2;
  std::cout << " TO_L2_BC_MSHR: " << pf_to_l2_bc_mshr << std::endl;

  std::cout << "BERTI_TRI AVG_LAT: ";
  std::cout << average_latency.average << " NUM_TRACK_LATENCY: ";
  std::cout << average_latency.num << " NUM_CANT_TRACK_LATENCY: ";
  std::cout << cant_track_latency << std::endl;

  std::cout << "BERTI_TRI CROSS_PAGE " << cross_page;
  std::cout << " NO_CROSS_PAGE: " << no_cross_page << std::endl;

  std::cout << "BERTI_TRI";
  std::cout << " FOUND_BERTI: " << found_berti;
  std::cout << " NO_FOUND_BERTI: " << no_found_berti << std::endl;

  std::cout << "BERTI_TRI";
  std::cout << " AVERAGE_ISSUED: " << ((1.0 * average_issued) / average_num);
  std::cout << std::endl;

  std::cout << "BERTI_TRI DRAM_ROW_WARMING:";
  std::cout << " WARM_REQUESTS: " << dram_warm_requests;
  std::cout << " WARM_ISSUED: " << dram_row_warm_issued;
  std::cout << std::endl;

  row_scheduler->print_stats("Berti-Tri DRAM Row Warming Scheduler");
}