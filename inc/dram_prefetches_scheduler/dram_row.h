#pragma once
#include <cassert>
#include <memory>

#include "prefetch_request.h"
#include "row_identifier.h"

namespace dram_open
{

class DramRow
{
public:
  DramRow() = default;

  DramRow(const RowIdentifier& id, PrefetchRequestPtr req) : rowIdentifier_{id}, latestRequest_{std::move(req)}, consecutiveAccesses_{0}
  {
    assert(latestRequest_ && "Initial request must be non-null");
  }

  bool addRequest(PrefetchRequestPtr req)
  {
    assert(req && "Adding null request");
    assert(latestRequest_ && "Row has a latest request null");

    // Check if this is a duplicate (same block number)
    bool is_duplicate = (*latestRequest_ == *req);

    // Always update the latest request regardless
    latestRequest_ = std::move(req);

    // Return false for duplicates (for counting)
    return !is_duplicate;
  }

  std::uint64_t recordAccess(std::uint64_t cycle) noexcept
  {
    assert(latestRequest_ && "No request recorded");
    return latestRequest_->get_delay(cycle);
  }

  float getConfidence() const noexcept
  {
    assert(latestRequest_ && "No request recorded");
    return latestRequest_->get_confidence();
  }

  std::uint32_t getConfidenceLevel() const noexcept
  {
    assert(latestRequest_ && "No request recorded");
    return latestRequest_->get_confidence_level();
  }

  const RowIdentifier& getRowIdentifier() const noexcept { return rowIdentifier_; }

  // Consecutive access tracking
  void incrementConsecutiveAccesses() noexcept { ++consecutiveAccesses_; }
  void resetConsecutiveAccesses() noexcept { consecutiveAccesses_ = 0; }
  std::uint64_t getConsecutiveAccesses() const noexcept { return consecutiveAccesses_; }

private:
  RowIdentifier rowIdentifier_{};
  PrefetchRequestPtr latestRequest_{};
  std::uint64_t consecutiveAccesses_{0};
};

} // namespace dram_open