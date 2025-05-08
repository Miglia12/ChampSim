#pragma once

#include <cstdint>

namespace dram_open
{
namespace parameters
{

// DRAM scheduler parameters
inline constexpr std::size_t DRAM_ROW_SIZE = 128;       // Maximum prefetch requests per DRAM row
inline constexpr std::uint64_t BANK_CYCLE_DELAY = 10000;   // Required cycles between accesses to same bank
inline constexpr std::uint64_t SLACK_CYCLES = 50;      // Tolerance for late prefetch requests
inline constexpr std::uint32_t MAXIMUM_CONFIDENCE_LEVEL = 1; // Maximum confidence level for normalization
inline constexpr float SCHEDULER_ISSUE_RATE = 0.5;          // Max percentage of PQ to use

// DRAM Controller parameters
inline constexpr bool DRAM_ROW_OPEN_PAYS_TCAS = false;        //  Set to true if speculative opens should pay tCAS
inline constexpr bool use_row_buffer_aware_controller = true; // Controls whether the DRAM controller considers potential row-buffer hits for scheduling
inline constexpr bool perfect_speculative_opening = false;  // Enables perfect speculative opening mode
inline constexpr bool enable_dram_controller_access = false

; // When true, use DRAM controller to check bank status

} // namespace parameters
} // namespace dram_open
