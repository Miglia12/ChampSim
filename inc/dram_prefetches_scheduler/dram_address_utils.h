#pragma once
#include <cstdint>
#include <functional>

#include "address.h"

namespace dram_open
{

struct RowIdentifier {
  unsigned long channel;
  unsigned long rank;
  unsigned long bankGroup;
  unsigned long bank;

  bool operator==(const RowIdentifier& other) const
  {
    return channel == other.channel && rank == other.rank && bankGroup == other.bankGroup && bank == other.bank;
  }

  bool operator!=(const RowIdentifier& other) const { return !(*this == other); }
};

// Define interface types for DRAM address mapping functions
using DramAddressFunction = std::function<unsigned long(champsim::address)>;

// Global function pointers that will be set by MEMORY_CONTROLLER
extern DramAddressFunction get_channel_func;
extern DramAddressFunction get_rank_func;
extern DramAddressFunction get_bankgroup_func;
extern DramAddressFunction get_bank_func;
extern DramAddressFunction get_row_func;
extern DramAddressFunction get_column_func;

// Wrapper functions that use the function pointers if set, otherwise return default values
inline unsigned long get_dram_channel(champsim::address addr) { return get_channel_func ? get_channel_func(addr) : 0; }

inline unsigned long get_dram_rank(champsim::address addr) { return get_rank_func ? get_rank_func(addr) : 0; }

inline unsigned long get_bankgroup(champsim::address addr) { return get_bankgroup_func ? get_bankgroup_func(addr) : 0; }

inline unsigned long get_bank(champsim::address addr) { return get_bank_func ? get_bank_func(addr) : 0; }

inline unsigned long get_row(champsim::address addr) { return get_row_func ? get_row_func(addr) : 0; }

inline unsigned long get_column(champsim::address addr) { return get_column_func ? get_column_func(addr) : 0; }

static RowIdentifier to_RowIdentifier(champsim::address addr) noexcept
{
  return {get_dram_channel(addr), get_dram_rank(addr), get_bankgroup(addr), get_bank(addr)};
}

} // namespace dram_open