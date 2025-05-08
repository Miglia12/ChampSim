#pragma once
#include <cstdint>

#include "address.h"
#include "dram_controller.h"

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

inline unsigned long get_dram_channel(champsim::address addr)
{
  if (MEMORY_CONTROLLER::dram_controller_static)
    return MEMORY_CONTROLLER::dram_controller_static->get_address_mapping().get_channel(addr);
  return 0;
}

inline unsigned long get_dram_rank(champsim::address addr)
{
  if (MEMORY_CONTROLLER::dram_controller_static)
    return MEMORY_CONTROLLER::dram_controller_static->get_address_mapping().get_rank(addr);
  return 0;
}

inline unsigned long get_bankgroup(champsim::address addr)
{
  if (MEMORY_CONTROLLER::dram_controller_static)
    return MEMORY_CONTROLLER::dram_controller_static->get_address_mapping().get_bankgroup(addr);
  return 0;
}

inline unsigned long get_bank(champsim::address addr)
{
  if (MEMORY_CONTROLLER::dram_controller_static)
    return MEMORY_CONTROLLER::dram_controller_static->get_address_mapping().get_bank(addr);
  return 0;
}

inline unsigned long get_row(champsim::address addr)
{
  if (MEMORY_CONTROLLER::dram_controller_static)
    return MEMORY_CONTROLLER::dram_controller_static->get_address_mapping().get_row(addr);
  return 0;
}

inline unsigned long get_column(champsim::address addr)
{
  if (MEMORY_CONTROLLER::dram_controller_static)
    return MEMORY_CONTROLLER::dram_controller_static->get_address_mapping().get_column(addr);
  return 0;
}

static RowIdentifier to_RowIdentifier(champsim::address addr) noexcept
{
  return {get_dram_channel(addr), get_dram_rank(addr), get_bankgroup(addr), get_bank(addr)};
}

} // namespace dram_open
