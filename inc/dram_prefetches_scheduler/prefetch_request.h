#pragma once

#include <cassert>
#include <cstdint>
#include <memory>

#include "address.h"
#include "scheduler_parameters.h"

namespace dram_open
{

/**
 * Represents a single prefetch request to the DRAM system
 */
class PrefetchRequest
{
public:
  // Constructor for a prefetch request with specified address, confidence, and ready time
  PrefetchRequest(champsim::address address, std::uint32_t confidenceLevel, std::uint64_t readyCycle) noexcept
      : address_(address), readyCycle_(readyCycle), servicedCycle_(0)
  {
    assert(confidenceLevel <= parameters::MAXIMUM_CONFIDENCE_LEVEL && "Higher than maximum confidence allows");
    (void)confidenceLevel;
  }

  // Returns true if the request is ready at the given cycle
  bool isReady(std::uint64_t currentCycle) const noexcept { return currentCycle >= readyCycle_; }

  // Returns true if the request has been serviced
  bool isServiced() const noexcept { return servicedCycle_ > 0; }

  // Marks this request as serviced at the given cycle
  std::uint64_t markServiced(std::uint64_t cycle) noexcept
  {
    assert(!isServiced() && "Request was serviced more than once");
    servicedCycle_ = cycle;
    assert(servicedCycle_ >= readyCycle_ && "Request was serviced before it became ready");
    return servicedCycle_ - readyCycle_;
  }

  // Equality is defined by address and ready time (duplicate detection)
  bool operator==(const PrefetchRequest& rhs) const noexcept { return address_ == rhs.address_ && readyCycle_ == rhs.readyCycle_; }

  bool operator!=(const PrefetchRequest& rhs) const noexcept { return !(*this == rhs); }

private:
  champsim::address address_;   // Memory address for this request
  std::uint64_t readyCycle_;    // Cycle when this request becomes ready
  std::uint64_t servicedCycle_; // Cycle when this request was serviced (0 = not yet)
};

// Smart pointer type for prefetch requests
using PrefetchRequestPtr = std::shared_ptr<PrefetchRequest>;

} // namespace dram_open
