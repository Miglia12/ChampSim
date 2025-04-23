#pragma once
#include <algorithm>
#include <cstdint>
#include <functional>
#include <string_view>
#include <vector>

#include "dram_row_open_request.h"
#include "dram_row_open_stats.h"

namespace dram_open
{

// Type aliases
using cycle_t = uint64_t;
using request_callback_t = std::function<bool(const DramRowOpenRequest&)>;

class DramRowOpenScheduler
{
public:
  DramRowOpenScheduler(size_t max_size_param, cycle_t default_ready_delay = 0, cycle_t slack_cycles = 0)
      : m_max_size(max_size_param), m_default_ready_threshold(default_ready_delay), m_dynamic_threshold(0), m_slack(slack_cycles)
  {
  }

  void tick(cycle_t now, size_t max_issue, request_callback_t try_issue)
  {
    // Execute the four stages in sequence:
    // (Adding happens via add_request, then pruning, selecting, and issuing happen here)
    prune_expired_requests(now);
    issue_ready_candidates(now, max_issue, try_issue);
  }

  bool add_request(const DramRowOpenRequest& req, cycle_t now, cycle_t ready_delay = 0)
  {
    // Stage 1: Adding

    // Determine effective latency for this request
    cycle_t effective_latency;
    if (ready_delay > 0) {
      // If delay is provided, use it
      effective_latency = ready_delay;

      // Update dynamic threshold if delay is higher than default
      if (ready_delay > m_default_ready_threshold) {
        m_dynamic_threshold = ready_delay;
        m_stats.DYNAMIC_THRESHOLD_UPDATES++;
      }
    } else if (m_dynamic_threshold > m_default_ready_threshold) {
      // If no delay provided and dynamic threshold is higher than default, use dynamic
      effective_latency = m_dynamic_threshold;
    } else {
      // Otherwise use default threshold
      effective_latency = m_default_ready_threshold;
    }

    // Create timed entry with effective latency
    TimedEntry new_entry(req, now, effective_latency);

    // Check for duplicates on a page level
    auto it = std::find(m_queue.begin(), m_queue.end(), new_entry);
    if (it != m_queue.end()) {
      m_stats.DUPLICATES_DETECTED++;

      // Update confidence if new entry has higher confidence
      if (new_entry.request.confidence > it->request.confidence) {
        it->request.confidence = new_entry.request.confidence;
        m_stats.CONFIDENCE_UPDATES++;
      }

      return false;
    }

    // Add to queue if space available
    if (m_queue.size() < m_max_size) {
      m_queue.push_back(new_entry);
      m_stats.REQUESTS_ADDED++;
      return true;
    }

    // Queue is full
    m_stats.DROPPED_FULL_QUEUE++;
    return false;
  }

  void add_requests(const std::vector<DramRowOpenRequest>& requests, cycle_t now)
  {
    for (const auto& req : requests)
      add_request(req, now);
  }

  [[nodiscard]] size_t size() const { return m_queue.size(); }
  [[nodiscard]] size_t capacity() const { return m_max_size; }
  [[nodiscard]] cycle_t get_default_threshold() const { return m_default_ready_threshold; }
  [[nodiscard]] cycle_t get_dynamic_threshold() const { return m_dynamic_threshold; }

  void clear() { m_queue.clear(); }
  void reset_stats() { m_stats.reset(); }
  [[nodiscard]] const SchedulerStats& get_stats() const { return m_stats; }

  void print_stats(std::string_view name = "DramRowOpenScheduler") const { m_stats.print(name, m_default_ready_threshold, m_dynamic_threshold); }

private:
  size_t m_max_size;
  cycle_t m_default_ready_threshold; // Base default value set in constructor
  cycle_t m_dynamic_threshold;       // Dynamically updated threshold
  cycle_t m_slack;
  std::vector<TimedEntry> m_queue;
  SchedulerStats m_stats;

  void prune_expired_requests(cycle_t now)
  {
    // Stage 2: Pruning
    auto deadline_passed = [now, this](const auto& entry) {
      return now > (entry.inserted_at + entry.issue_latency + m_slack);
    };

    auto begin_remove = std::remove_if(m_queue.begin(), m_queue.end(), deadline_passed);
    m_stats.PRUNED_EXPIRED += std::distance(begin_remove, m_queue.end());
    m_queue.erase(begin_remove, m_queue.end());
  }

  void issue_ready_candidates(cycle_t now, size_t max_issue, request_callback_t try_issue)
  {
    // Stage 3: Selecting
    // First collect all ready requests
    std::vector<TimedEntry> ready_entries;
    ready_entries.reserve(std::min(m_queue.size(), max_issue));

    for (const auto& entry : m_queue) {
      if (now - entry.inserted_at >= entry.issue_latency)
        ready_entries.push_back(entry);
    }

    // Sort by confidence
    std::sort(ready_entries.begin(), ready_entries.end(), [](const auto& a, const auto& b) { return a.request.confidence > b.request.confidence; });

    // Limit to max_issue
    if (ready_entries.size() > max_issue)
      ready_entries.resize(max_issue);

    // Stage 4: Issuing
    for (const auto& candidate : ready_entries) {
      if (try_issue(candidate.request)) {
        auto it = std::find(m_queue.begin(), m_queue.end(), candidate);
        if (it != m_queue.end()) {
          m_queue.erase(it);
          m_stats.ISSUED_SUCCESS++;
        }
      } else {
        m_stats.ISSUE_FAILURES++;
      }
    }
  }
};

} // namespace dram_open