#pragma once
#include <cstdint>

#include "address.h"

namespace dram_open
{

struct RowIdentifier {
  unsigned long channel;
  unsigned long rank;
  unsigned long bankGroup;
  unsigned long bank;
  unsigned long row;

  bool operator==(const RowIdentifier& other) const
  {
    return channel == other.channel && rank == other.rank && bankGroup == other.bankGroup && bank == other.bank
           && row == other.row;
  }

  bool operator!=(const RowIdentifier& other) const { return !(*this == other); }
};

} // namespace dram_open