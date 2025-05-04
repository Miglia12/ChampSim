#pragma once
#include <algorithm>
#include <cassert>
#include <limits>
#include <map>
#include <memory>
#include <unordered_map>
#include <vector>

#include "address.h"
#include "dram_row_open_stats.h"
#include "dram_row_open_utils.h"

namespace dram_open
{

/**
 * Structure representing DRAM coordinates (channel, rank, bank, row)
 * Used as a map key for organizing DRAM rows
 */
struct DramCoordinates {
  unsigned long channel;
  unsigned long rank;
  unsigned long bank;
  unsigned long row;

  // Constructor from address
  explicit DramCoordinates(champsim::address addr) : channel(get_dram_channel(addr)), rank(get_dram_rank(addr)), bank(get_bank(addr)), row(get_row(addr)) {}

  // Comparator remains the same
  bool operator<(const DramCoordinates& other) const
  {
    if (channel != other.channel)
      return channel < other.channel;
    if (rank != other.rank)
      return rank < other.rank;
    if (bank != other.bank)
      return bank < other.bank;
    return row < other.row;
  }
};
/**
 * Represents a DRAM row open request for prefetching.
 */
struct DramRowOpenRequest {
  champsim::address addr;   // The memory address to prefetch
  uint32_t confidence;      // Confidence score for this prefetch
  uint32_t metadata;        // Prefetcher metadata to be passed to cache on issue
  uint64_t issue_cycle = 0; // Cycle when this request was issued

  DramRowOpenRequest(champsim::address address, uint32_t conf, uint32_t meta = 0) : addr(address), confidence(conf), metadata(meta) {}

  DramRowOpenRequest() = default;

  // Compare requests by confidence (for prioritization)
  bool operator<(const DramRowOpenRequest& other) const { return confidence < other.confidence; }

  // Compare requests by address (for deduplication)
  bool operator==(const DramRowOpenRequest& other) const { return champsim::block_number{addr} == champsim::block_number{other.addr}; }

  // Get the block number for this request
  champsim::block_number get_block_number() const { return champsim::block_number{addr}; }
};

/**
 * Represents a DRAM row with associated prefetch requests
 * Manages prefetch requests and prioritization
 */
class DramRow
{
private:
  std::vector<DramRowOpenRequest> prefetch_requests;
  float priority_score = 0.0f;

public:
  // Default constructor
  DramRow() = default;

  // Constructor with initial request
  explicit DramRow(const DramRowOpenRequest& req)
  {
    prefetch_requests.push_back(req);
  }

  // Request management
  bool empty() const { return prefetch_requests.empty(); }
  size_t size() const { return prefetch_requests.size(); }
  float get_score() const { return priority_score; }
  void set_score(float score) { priority_score = score; }
  const std::vector<DramRowOpenRequest>& get_prefetches() const { return prefetch_requests; }

  /**
   * Calculate score based on row density and confidence
   * @param density_weight Weight for density component (0-1)
   * @param confidence_weight Weight for confidence component (0-1)
   * @param max_confidence Maximum possible confidence for normalization
   * @param row_buffer_size Size of DRAM row buffer for density calculation
   */
  void calculate_score(float density_weight, float confidence_weight, uint32_t max_confidence, uint32_t row_buffer_size)
  {
    // Calculate normalized density based on row buffer size
    float density = std::min(static_cast<float>(size()) / static_cast<float>(row_buffer_size), 1.0f);

    // Calculate average confidence normalized by max possible confidence
    float avg_confidence = 0.0f;
    if (!empty()) {
      float sum = 0.0f;
      for (const auto& pf : prefetch_requests)
        sum += static_cast<float>(pf.confidence);
      avg_confidence = (sum / static_cast<float>(size())) / static_cast<float>(max_confidence);
    }

    // Calculate weighted score
    priority_score = density_weight * density + confidence_weight * avg_confidence;
  }

  /**
   * Add a prefetch request to this row, handling duplicates
   * @param req The request to add
   * @return true if it was a new request, false if duplicate (confidence/metadata may be updated)
   */
  bool add_prefetch(const DramRowOpenRequest& req)
  {
    // Check for duplicate (same block address) using the == operator
    for (auto& existing : prefetch_requests) {
      if (existing == req) {
        // Update confidence and metadata if higher confidence
        if (req.confidence > existing.confidence) {
          existing.confidence = req.confidence;
          existing.metadata = req.metadata;
        }
        return false; // Duplicate, no need to add
      }
    }

    // No duplicate found, add the prefetch
    prefetch_requests.push_back(req);
    return true; // Added new request
  }

  /**
   * Get the highest confidence prefetch request in this row
   * @return Pointer to the highest confidence request, nullptr if empty
   */
  const DramRowOpenRequest* get_highest_confidence_prefetch() const
  {
    if (prefetch_requests.empty())
      return nullptr;

    return &*std::max_element(prefetch_requests.begin(), prefetch_requests.end(), [](const auto& a, const auto& b) { return a.confidence < b.confidence; });
  }
};

/**
 * Lightweight structure for row candidates in the balanced issuing algorithm
 * Stores a pointer to the row, its score, and its coordinates
 */
struct RowCandidate {
  DramRow* row;           // Pointer to the row
  float score;            // Priority score
  DramCoordinates coords; // DRAM coordinates (channel, rank, bank, row)

  // Higher score first for priority queue ordering
  bool operator<(const RowCandidate& other) const { return score > other.score; }
};

/**
 * A group of prefetch requests that become ready at the same cycle
 * Manages a collection of DRAM rows with prefetch requests
 */
class ReadyGroup
{
private:
  // Map of DRAM coordinates to DRAM rows
  std::map<DramCoordinates, DramRow> ready_rows;

public:
  /**
   * Add a prefetch request to this ready group
   * @return true if this was a new request
   */
  bool add_prefetch(const DramRowOpenRequest& req, float density_weight, float confidence_weight, uint32_t max_confidence, uint32_t row_buffer_size,
                    SchedulerStats& stats)
  {
    // Create coordinates for lookup
    DramCoordinates coords(req.addr);

    // Try to find existing row
    auto it = ready_rows.find(coords);
    bool is_new_request = false;

    if (it != ready_rows.end()) {
      // Row exists, try to add prefetch
      DramRow& row = it->second;

      // Add the prefetch
      is_new_request = row.add_prefetch(req);

      // Update statistics
      if (is_new_request) {
        stats.REQUESTS_ADDED++;
      } else {
        stats.DUPLICATES_DETECTED++;
      }

      // Always update score
      row.calculate_score(density_weight, confidence_weight, max_confidence, row_buffer_size);
    } else {
      // Create new row with the request directly
      auto [new_it, _] = ready_rows.emplace(coords, DramRow(req));
      DramRow& row = new_it->second;

      // Calculate the new row's score
      row.calculate_score(density_weight, confidence_weight, max_confidence, row_buffer_size);

      is_new_request = true;
      stats.REQUESTS_ADDED++;
    }

    return is_new_request;
  }

  // Collection information
  size_t size() const
  {
    size_t count = 0;
    for (const auto& [_, row] : ready_rows) {
      count += row.size();
    }
    return count;
  }

  bool empty() const { return ready_rows.empty(); }

  // Access to the rows
  const std::map<DramCoordinates, DramRow>& get_rows() const { return ready_rows; }
  std::map<DramCoordinates, DramRow>& get_rows() { return ready_rows; }
};

}