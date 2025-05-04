#pragma once
#include <algorithm>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "dram_row_open_data_structures.h"
#include "dram_row_open_stats.h"

namespace dram_open
{

// Type aliases for clarity
using cycle_t = uint64_t;
using request_callback_t = std::function<bool(const DramRowOpenRequest&)>;

/**
 * Class to track and balance DRAM component usage during prefetch issuance
 */
class DramUsageTracker
{
private:
  std::unordered_map<unsigned, unsigned> channel_usage;
  std::unordered_map<unsigned, unsigned> rank_usage;
  std::unordered_map<unsigned, unsigned> bank_usage;

public:
  // Get usage counts
  unsigned get_channel_usage(unsigned channel) const
  {
    auto it = channel_usage.find(channel);
    return it != channel_usage.end() ? it->second : 0;
  }

  unsigned get_rank_usage(unsigned rank) const
  {
    auto it = rank_usage.find(rank);
    return it != rank_usage.end() ? it->second : 0;
  }

  bool is_bank_in_use(unsigned bank) const
  {
    auto it = bank_usage.find(bank);
    return it != bank_usage.end() && it->second > 0;
  }

  // Record usage of DRAM components
  void record_usage(const DramCoordinates& coords)
  {
    channel_usage[static_cast<unsigned>(coords.channel)]++;
    rank_usage[static_cast<unsigned>(coords.rank)]++;
    bank_usage[static_cast<unsigned>(coords.bank)]++;
  }
};

/**
 * Scheduler for DRAM row open requests
 * Manages and prioritizes row open requests with balancing across DRAM channels, ranks, and banks
 */
class DramRowOpenScheduler
{
public:
  /**
   * Constructor with configurable parameters
   * @param max_queue_size_param Maximum number of requests in the scheduler
   * @param slack_cycles Additional time a request remains in the queue after becoming ready
   * @param density_weight Weight of request density in prioritization (0-1)
   * @param confidence_weight Weight of request confidence in prioritization (0-1)
   * @param max_confidence Maximum possible confidence value for normalization
   * @param row_buffer_size Size of DRAM row buffer for density calculation
   */
  DramRowOpenScheduler(size_t max_queue_size_param, cycle_t slack_cycles, float density_weight_param = 0.6f, float confidence_weight_param = 0.4f,
                       uint32_t max_confidence_param = 100, uint32_t row_buffer_size_param = 64)
      : max_queue_size(max_queue_size_param), time_slack(slack_cycles), density_weight(density_weight_param), confidence_weight(confidence_weight_param),
        max_confidence(max_confidence_param), row_buffer_size(row_buffer_size_param)
  {}

  /**
   * Process the scheduler state each cycle
   * @param now Current cycle timestamp
   * @param max_issue Maximum number of requests to issue this cycle
   * @param try_issue Callback to try issuing a request
   */
  void tick(cycle_t now, size_t max_issue, request_callback_t try_issue)
  {
    remove_expired_ready_groups(now);
    issue_prefetches(now, max_issue, try_issue);
  }

  /**
   * Add a row open request with optional delay
   * @param req The DRAM row open request (containing address, confidence, and metadata)
   * @param now Current cycle timestamp
   * @param delay Delay in cycles before the request is considered ready (0 means immediate)
   * @return True if request was added, false if dropped
   */
  bool add_request(const DramRowOpenRequest& req, cycle_t now, cycle_t delay)
  {
    // Check capacity
    if (total_prefetch_count() >= max_queue_size) {
      m_stats.DROPPED_FULL_QUEUE++;
      return false;
    }

    // Calculate ready cycle
    cycle_t ready_cycle = now + delay;

    // Get or create the ready group for this cycle
    if (ready_groups.find(ready_cycle) == ready_groups.end()) {
      ready_groups[ready_cycle] = ReadyGroup();
    }

    // Add the prefetch to the ready group and return result
    return ready_groups[ready_cycle].add_prefetch(req, density_weight, confidence_weight, max_confidence, row_buffer_size, m_stats);
  }

  // Collection management
  [[nodiscard]] size_t size() const { return total_prefetch_count(); }
  [[nodiscard]] size_t capacity() const { return max_queue_size; }
  void clear() { ready_groups.clear(); }

  // Statistics
  void reset_stats() { m_stats.reset(); }
  [[nodiscard]] const SchedulerStats& get_stats() const { return m_stats; }
  void print_stats(std::string_view name = "DramRowOpenScheduler") const { m_stats.print(name); }

private:
  // Configuration parameters
  size_t max_queue_size;
  cycle_t time_slack;
  const float density_weight;
  const float confidence_weight;
  const uint32_t max_confidence;
  const uint32_t row_buffer_size;

  // Core data structures
  std::map<cycle_t, ReadyGroup> ready_groups; // Ready cycle -> ReadyGroup
  SchedulerStats m_stats;

  /**
   * Get total number of prefetch requests in the scheduler
   */
  size_t total_prefetch_count() const
  {
    size_t count = 0;
    for (const auto& [_, ready_group] : ready_groups) {
      count += ready_group.size();
    }
    return count;
  }

  /**
   * Remove ready groups that have expired based on slack time
   */
  void remove_expired_ready_groups(cycle_t current_cycle)
  {
    std::vector<cycle_t> expired_cycles;

    // Find expired ready groups
    for (const auto& [cycle, ready_group] : ready_groups) {
      if (current_cycle > cycle + time_slack) {
        m_stats.PRUNED_EXPIRED += ready_group.size();
        expired_cycles.push_back(cycle);
      }
    }

    // Remove expired ready groups
    for (cycle_t cycle : expired_cycles)
      ready_groups.erase(cycle);
  }

  /**
   * Issue prefetches with balanced DRAM parallelism
   * Tries to maximize channel, rank, and bank parallelism
   */
  void issue_prefetches(cycle_t current_cycle, size_t max_issue, request_callback_t try_issue)
  {
    if (ready_groups.empty() || max_issue == 0)
      return;

    size_t issued_count = 0;
    std::vector<cycle_t> empty_cycles;

    // Process ready groups in chronological order
    for (auto& [cycle, ready_group] : ready_groups) {
      // Skip if not ready yet
      if (cycle > current_cycle)
        continue;

      // Skip if empty
      if (ready_group.empty()) {
        empty_cycles.push_back(cycle);
        continue;
      }

      issued_count += issue_from_ready_group(ready_group, cycle, current_cycle, max_issue - issued_count, try_issue);

      // Mark empty ready groups for removal
      if (ready_group.empty())
        empty_cycles.push_back(cycle);

      // Stop if we've issued the maximum number of prefetches
      if (issued_count >= max_issue)
        break;
    }

    // Remove empty ready groups
    for (cycle_t cycle : empty_cycles)
      ready_groups.erase(cycle);

    m_stats.ISSUED_SUCCESS += issued_count;
  }

  /**
   * Issue prefetches from a single ready group
   * @return Number of prefetches successfully issued
   */
  size_t issue_from_ready_group(ReadyGroup& ready_group, cycle_t ready_cycle, cycle_t current_cycle, size_t max_to_issue, request_callback_t try_issue)
  {
    // Collect all eligible row candidates with their scores and coordinates
    std::vector<RowCandidate> candidates;
    std::vector<DramCoordinates> row_keys;

    for (auto& [coords, row] : ready_group.get_rows()) {
      if (!row.empty()) {
        row_keys.push_back(coords);
        // Store coordinates in the candidate for access during selection
        candidates.push_back({&row, row.get_score(), coords});
      }
    }

    // Sort candidates by score (descending)
    std::sort(candidates.begin(), candidates.end());

    // Track DRAM component usage for balanced issuing
    DramUsageTracker usage_tracker;

    // Track rows to remove after issuing
    std::vector<DramCoordinates> rows_to_remove;

    // Issue requests with balanced parallelism
    size_t remaining = std::min(max_to_issue, candidates.size());
    size_t issued_count = 0;

    while (remaining > 0) {
      // Find the best candidate considering parallelism
      int best_index = find_best_candidate(candidates, usage_tracker);

      // No more candidates that satisfy constraints
      if (best_index == -1)
        break;

      // Get row and coordinates
      DramRow* row = candidates[best_index].row;
      const DramCoordinates& coords = candidates[best_index].coords;

      const DramRowOpenRequest* pf = row->get_highest_confidence_prefetch();

      if (pf && try_issue(*pf)) {
        // Update statistics - calculate delay based on the ready cycle
        m_stats.TOTAL_DELAY_CYCLES += ready_cycle > current_cycle ? ready_cycle - current_cycle : 0;

        // Update usage counters for balancing
        usage_tracker.record_usage(coords);

        // Store coordinates for removal
        rows_to_remove.push_back(row_keys[best_index]);

        // Mark as processed
        candidates[best_index].row = nullptr;

        issued_count++;
        remaining--;
      } else {
        m_stats.ISSUE_FAILURES++;
        // Mark as failed to avoid retrying
        candidates[best_index].row = nullptr;
      }
    }

    // Remove issued rows
    for (const auto& coords : rows_to_remove) {
      ready_group.get_rows().erase(coords);
    }

    return issued_count;
  }

  /**
   * Find the best candidate considering DRAM parallelism constraints
   * @return Index of the best candidate, -1 if none found
   */
  int find_best_candidate(const std::vector<RowCandidate>& candidates, const DramUsageTracker& usage_tracker)
  {
    int best_index = -1;
    unsigned min_channel_usage = std::numeric_limits<unsigned>::max();
    unsigned min_rank_usage = std::numeric_limits<unsigned>::max();

    // Find candidate with best DRAM parallelism
    for (std::vector<RowCandidate>::size_type i = 0; i < candidates.size(); i++) {
      DramRow* row = candidates[i].row;
      const DramCoordinates& coords = candidates[i].coords;

      // Skip if already issued or invalid
      if (!row || row->empty())
        continue;

      // Avoid multiple requests to the same bank
      if (usage_tracker.is_bank_in_use(static_cast<unsigned>(coords.bank)))
        continue;

      // Get current usage levels
      unsigned channel_count = usage_tracker.get_channel_usage(static_cast<unsigned>(coords.channel));
      unsigned rank_count = usage_tracker.get_rank_usage(static_cast<unsigned>(coords.rank));

      // Select based on parallelism priority: channel, then rank, then bank
      bool is_better = false;

      if (channel_count < min_channel_usage) {
        is_better = true;
      } else if (channel_count == min_channel_usage) {
        if (rank_count < min_rank_usage) {
          is_better = true;
        }
      }

      if (is_better) {
        min_channel_usage = channel_count;
        min_rank_usage = rank_count;
        best_index = static_cast<int>(i);
      }
    }

    return best_index;
  }
};

} // namespace dram_open