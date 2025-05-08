// dram_row.h
#pragma once

#include <algorithm>
#include <memory>
#include <optional>
#include <vector>

#include "dram_address_utils.h"
#include "prefetch_request.h"
#include "scheduler_parameters.h"

namespace dram_open
{

class DramRow
{
public:
  // Private default constructor only for container usage
  DramRow() = default;

  // Primary constructor that requires a row identifier and initial request
  DramRow(const RowIdentifier& rowId, PrefetchRequestPtr initialRequest) : rowIdentifier_(rowId)
  {
    // Add the initial request
    requests_.push_back(initialRequest);
  }

  // Add a request to this row
  bool addRequest(PrefetchRequestPtr request)
  {
    // Check if row is full
    if (isFull()) {
      return false;
    }

    // Check if we already have a request for this address
    auto it = std::find_if(requests_.begin(), requests_.end(),
                           [&request](const PrefetchRequestPtr& existingRequest) { return existingRequest->getAddress() == request->getAddress(); });

    if (it != requests_.end()) {
      // If new request has higher confidence, replace the existing one
      if (request->getConfidenceLevel() > (*it)->getConfidenceLevel()) {
        *it = request;
        return true;
      }
      return false; // Lower confidence, don't replace
    }

    // Add new request
    requests_.push_back(request);
    return true;
  }

  // Calculate the score of this row
  float calculateScore(std::uint64_t currentCycle) const
  {
    auto readyRequests = getReadyRequests(currentCycle);
    if (readyRequests.empty()) {
      return 0.0f;
    }

    // Calculate sum of confidence levels
    float totalConfidence = 0.0f;
    for (const auto& request : readyRequests) {
      totalConfidence += request->getConfidenceLevel();
    }

    // Average confidence weighted by row occupancy
    return (totalConfidence / static_cast<float>(readyRequests.size()))
           * (static_cast<float>(readyRequests.size()) / static_cast<float>(parameters::DRAM_ROW_SIZE));
  }

  // Prune expired requests
  std::size_t pruneExpiredRequests(std::uint64_t currentCycle)
  {
    std::size_t countBefore = requests_.size();

    requests_.erase(
        std::remove_if(requests_.begin(), requests_.end(), [currentCycle](const PrefetchRequestPtr& request) { return request->isExpired(currentCycle); }),
        requests_.end());

    return countBefore - requests_.size();
  }

  // Get the highest confidence ready request
  std::optional<PrefetchRequestPtr> getHighestConfidenceRequest(std::uint64_t currentCycle) const
  {
    std::optional<PrefetchRequestPtr> highestConfidenceRequest = std::nullopt;
    float highestConfidence = -1.0f;

    for (const auto& request : requests_) {
      if (request->isReady(currentCycle) && request->getConfidenceLevel() > highestConfidence) {
        highestConfidence = request->getConfidenceLevel();
        highestConfidenceRequest = request;
      }
    }

    return highestConfidenceRequest;
  }

  // Clear all ready requests and return count
  std::size_t clearReadyRequests(std::uint64_t currentCycle)
  {
    std::size_t countBefore = requests_.size();

    requests_.erase(
        std::remove_if(requests_.begin(), requests_.end(), [currentCycle](const PrefetchRequestPtr& request) { return request->isReady(currentCycle); }),
        requests_.end());

    return countBefore - requests_.size();
  }

  // Getters
  const RowIdentifier& getRowIdentifier() const noexcept { return rowIdentifier_; }

  std::size_t getSize() const noexcept { return requests_.size(); }

  std::size_t getReadyRequestCount(std::uint64_t currentCycle) const noexcept
  {
    return std::count_if(requests_.begin(), requests_.end(), [currentCycle](const PrefetchRequestPtr& request) { return request->isReady(currentCycle); });
  }

  bool isFull() const noexcept { return requests_.size() >= parameters::DRAM_ROW_SIZE; }

private:
  RowIdentifier rowIdentifier_;
  std::vector<PrefetchRequestPtr> requests_;

  // Helper to find ready requests
  std::vector<PrefetchRequestPtr> getReadyRequests(std::uint64_t currentCycle) const
  {
    std::vector<PrefetchRequestPtr> readyRequests;

    std::copy_if(requests_.begin(), requests_.end(), std::back_inserter(readyRequests),
                 [currentCycle](const PrefetchRequestPtr& request) { return request->isReady(currentCycle); });

    return readyRequests;
  }
};

} // namespace dram