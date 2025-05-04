#pragma once
#include <cstddef>
#include <cstdint>

namespace dram_open
{

// Scheduler queue parameters
static constexpr std::size_t SCHEDULER_QUEUE_SIZE = 126;
static constexpr uint64_t SCHEDULER_SLACK_CYCLES = 1;
static constexpr double SCHEDULER_ISSUE_RATE = 0.5;

// Scoring parameters
static constexpr float DENSITY_WEIGHT = 0.6f;    // 60% weight for row density
static constexpr float CONFIDENCE_WEIGHT = 0.4f; // 40% weight for confidence
static constexpr uint32_t MAX_CONFIDENCE = 16;  // Maximum confidence value
static constexpr uint32_t ROW_BUFFER_SIZE = 128; // Typical DRAM row buffer size

// Additional configuration parameters can be added here

} // namespace dram_open