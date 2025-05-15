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

namespace std
{
template <>
struct hash<dram_open::RowIdentifier> {
  size_t operator()(const dram_open::RowIdentifier& id) const
  {
    size_t h = 0;
    h = id.row;

    h ^= id.channel << 28;
    h ^= id.rank << 24;
    h ^= id.bankGroup << 20;
    h ^= id.bank << 16;

    return h;
  }
};
} // namespace std