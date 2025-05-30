#pragma once

#include <cstdint>

namespace dram_open
{
namespace parameters
{

// DRAM scheduler parameters
inline constexpr std::uint32_t MAXIMUM_CONFIDENCE_LEVEL = 6; // Maximum confidence level for normalization

// DRAM Controller parameters
inline constexpr bool DRAM_ROW_OPEN_PAYS_TCAS = false;         //  Set to true if speculative opens should pay tCAS
inline constexpr bool use_row_buffer_aware_controller = false; // Controls whether the DRAM controller considers potential row-buffer hits for scheduling
inline constexpr bool perfect_speculative_opening = false;     // Enables perfect speculative opening mode

// Scheduler refresh synchronization
inline constexpr bool SYNC_SCHEDULER_WITH_REFRESH = false;// When true, clears scheduler rows on DRAM refresh

// Scheduler accuracy constraint
inline constexpr bool ENFORCE_BANK_IDLE_CONSTRAINT = true; // When true, only marks scheduler hits as useful when target bank is idle

} // namespace parameters
} // namespace dram_open