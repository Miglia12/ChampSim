#pragma once
#include <cstdint>

#include "address.h"
#include "dram_controller.h"

namespace dram_open
{

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

} // namespace dram_open
