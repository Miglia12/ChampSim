#pragma once

#include <cstdint>

namespace dram_open
{
namespace parameters
{

// DRAM scheduler parameters
inline constexpr std::uint32_t MAXIMUM_CONFIDENCE_LEVEL = 6; // Maximum confidence level for normalization

// DRAM Controller parameters
inline constexpr bool DRAM_ROW_OPEN_PAYS_TCAS = false;        //  Set to true if speculative opens should pay tCAS
inline constexpr bool use_row_buffer_aware_controller = true; // Controls whether the DRAM controller considers potential row-buffer hits for scheduling
inline constexpr bool perfect_speculative_opening = false;    // Enables perfect speculative opening mode

// Scheduler refresh synchronization
inline constexpr bool SYNC_SCHEDULER_WITH_REFRESH = false; // When true, clears scheduler rows on DRAM refresh

// Number of power-of-2 buckets (excluding the 0 bucket)
inline constexpr std::size_t HISTOGRAM_BUCKETS = 10; // Total buckets = 1 (for 0) + POWER_OF_TWO_HISTOGRAM_BUCKETS

} // namespace parameters
} // namespace dram_open