#pragma once

#include <cstdint>
#include <memory>

#include "address.h"
#include "scheduler_parameters.h"

namespace dram_open
{

class PrefetchRequest
{
public:
  PrefetchRequest(champsim::address address, std::uint32_t confidenceLevel, std::uint64_t issueTime)
      : address_(address),
        // Normalize confidence level
        confidenceLevel_(static_cast<float>(std::min(confidenceLevel, parameters::MAXIMUM_CONFIDENCE_LEVEL)) / parameters::MAXIMUM_CONFIDENCE_LEVEL),
        issueTime_(issueTime), expirationTime_(issueTime + parameters::SLACK_CYCLES)
  {
  }

  // Getters
  champsim::address getAddress() const noexcept { return address_; }

  float getConfidenceLevel() const noexcept { return confidenceLevel_; }

  std::uint64_t getIssueTime() const noexcept { return issueTime_; }

  std::uint64_t getExpirationTime() const noexcept { return expirationTime_; }

  // Status checks
  bool isReady(std::uint64_t currentCycle) const noexcept { return currentCycle >= issueTime_; }

  bool isExpired(std::uint64_t currentCycle) const noexcept { return currentCycle > expirationTime_; }

private:
  champsim::address address_; 
  float confidenceLevel_;
  std::uint64_t issueTime_;
  std::uint64_t expirationTime_;
};

using PrefetchRequestPtr = std::shared_ptr<PrefetchRequest>;

} // namespace dram