#include "dram_stats.h"

dram_stats operator-(dram_stats lhs, dram_stats rhs)
{
  lhs.dbus_cycle_congested -= rhs.dbus_cycle_congested;
  lhs.dbus_count_congested -= rhs.dbus_count_congested;
  lhs.WQ_ROW_BUFFER_HIT -= rhs.WQ_ROW_BUFFER_HIT;
  lhs.WQ_ROW_BUFFER_MISS -= rhs.WQ_ROW_BUFFER_MISS;
  lhs.RQ_ROW_BUFFER_HIT -= rhs.RQ_ROW_BUFFER_HIT;
  lhs.RQ_ROW_BUFFER_MISS -= rhs.RQ_ROW_BUFFER_MISS;
  lhs.WQ_FULL -= rhs.WQ_FULL;

  lhs.DRAM_ROW_OPEN_REQUESTS -= rhs.DRAM_ROW_OPEN_REQUESTS;
  lhs.DRAM_ROW_OPEN_USEFUL -= rhs.DRAM_ROW_OPEN_USEFUL;
  lhs.DRAM_ROW_OPEN_USELESS -= rhs.DRAM_ROW_OPEN_USELESS;
  lhs.DRAM_ROW_OPEN_BANK_CONFLICT -= rhs.DRAM_ROW_OPEN_BANK_CONFLICT;

  return lhs;
}
