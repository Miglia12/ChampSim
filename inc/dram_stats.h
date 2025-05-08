#ifndef DRAM_STATS_H
#define DRAM_STATS_H

#include <cstdint>
#include <string>

struct dram_stats {
  std::string name{};
  long dbus_cycle_congested{};
  uint64_t dbus_count_congested = 0;
  uint64_t refresh_cycles = 0;
  unsigned WQ_ROW_BUFFER_HIT = 0, WQ_ROW_BUFFER_MISS = 0, RQ_ROW_BUFFER_HIT = 0, RQ_ROW_BUFFER_MISS = 0, WQ_FULL = 0;

  unsigned DRAM_ROW_OPEN_REQUESTS = 0; // Total speculative open requests
  unsigned DRAM_ROW_OPEN_USEFUL = 0;   // Hits on speculatively opened rows
  unsigned DRAM_ROW_OPEN_USELESS = 0;  // Speculatively opened rows closed without hits
  unsigned DRAM_ROW_OPEN_BANK_CONFLICT = 0; // Useless opens that caused bank conflicts
};

dram_stats operator-(dram_stats lhs, dram_stats rhs);

#endif
