#pragma once

#include <algorithm>
#include <cassert>
#include <memory>
#include <tuple>
#include <vector>

#include "dram_address_utils.h"
#include "prefetch_request.h"

namespace dram_open
{

class DramRow
{
public:
  DramRow() : accessed_(false) {}

  // Constructor with row identifier and initial request
  DramRow(const RowIdentifier& rowId, PrefetchRequestPtr initialRequest) : rowIdentifier_(rowId), accessed_(false)
  {
    assert(initialRequest && "Initial request cannot be null");
    requests_.push_back(initialRequest);
  }

  /**
   * Adds a new request to this row
   * @param request The request to add
   * @return true if added, false if there was a duplicate
   */
  bool addRequest(PrefetchRequestPtr request)
  {
    assert(request && "Attempting to add a null request");

    // Check for duplicate requests
    auto it = std::find_if(requests_.begin(), requests_.end(), [&request](const auto& existing) { return *existing == *request; });

    if (it != requests_.end())
      return false;

    requests_.push_back(request);
    return true;
  }

  /**
   * Records an access to this row
   * @param currentCycle The current cycle
   * @return A tuple of (serviced request count, last access latency)
   */
  std::tuple<std::uint64_t, std::uint64_t> recordAccess(std::uint64_t currentCycle) noexcept
  {
    std::uint64_t serviced_request_count = 0;
    std::uint64_t access_latency = std::numeric_limits<std::uint64_t>::max();

    for (auto& request : requests_) {
      if (request->isReady(currentCycle) && !request->isServiced()) {
        ++serviced_request_count;
        access_latency = std::min(access_latency, request->markServiced(currentCycle));
      }
    }

    return {serviced_request_count, access_latency};
  }

  /**
   * Removes requests that have been serviced
   * @return true if the row is now empty
   */
  bool removeServicedRequests() noexcept
  {
    requests_.erase(std::remove_if(requests_.begin(), requests_.end(), [](const PrefetchRequestPtr& req) { return req->isServiced(); }), requests_.end());
    return requests_.empty();
  }

  /**
   * Checks if this row has any ready and unserviced requests
   * @param currentCycle The current cycle
   * @return true if there are ready requests
   */
  bool hasReadyRequests(std::uint64_t currentCycle) const noexcept
  {
    return std::any_of(requests_.begin(), requests_.end(),
                       [currentCycle](const PrefetchRequestPtr& req) { return req->isReady(currentCycle) && !req->isServiced(); });
  }

  /**
   * Returns the number of requests in this row that are not yet serviced.
   */
  std::uint64_t outstandingRequestCount() const noexcept
  {
    return static_cast<std::uint64_t>(std::count_if(requests_.begin(), requests_.end(), [](const auto& p) { return !p->isServiced(); }));
  }

  /**
   * Returns the identifier of this DRAM row
   * @return Reference to the RowIdentifier
   */
  const RowIdentifier& getRowIdentifier() const noexcept { return rowIdentifier_; }

  bool wasAccessed() const noexcept { return accessed_; }
  void markAccessed() noexcept { accessed_ = true; }
  void resetAccessedFlag() noexcept { accessed_ = false; }

private:
  RowIdentifier rowIdentifier_;              // Identifier for this DRAM row
  std::vector<PrefetchRequestPtr> requests_; // All requests for this row
  bool accessed_;                            // true after first access
};

} // namespace dram_open
