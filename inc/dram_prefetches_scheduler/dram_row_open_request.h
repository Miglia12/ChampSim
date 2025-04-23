#pragma once
#include <cstdint>

#include "address.h"

namespace dram_open
{

// Type aliases
using metadata_t = uint32_t;
using confidence_t = unsigned int;
using cycle_t = uint64_t;

/**
 * A request to issue a row-open request with confidence tracking.
 */
struct DramRowOpenRequest {
  champsim::address addr;
  confidence_t confidence = 0;
  metadata_t metadata_in = 0;

  DramRowOpenRequest() = default;
  DramRowOpenRequest(champsim::address a, confidence_t conf = 0, metadata_t meta_data = 0)
      : addr(a), confidence(conf), metadata_in(meta_data)
  {
  }
};

/**
 * Represents a request with the time it was inserted.
 */
struct TimedEntry {
  DramRowOpenRequest request{};
  cycle_t inserted_at{};
  cycle_t issue_latency{};

  TimedEntry() = default;

  TimedEntry(const DramRowOpenRequest& req, cycle_t time, cycle_t latency) : request(req), inserted_at(time), issue_latency(latency) {}

  bool operator==(const TimedEntry& other) const
  {
    bool same_page = champsim::page_number{request.addr} == champsim::page_number{other.request.addr};
    bool same_time = inserted_at == other.inserted_at;
    return same_page && same_time;
  }

  bool operator!=(const TimedEntry& other) const { return !(*this == other); }
};

} // namespace dram_open