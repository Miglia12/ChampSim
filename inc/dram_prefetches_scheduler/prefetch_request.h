#pragma once

#include <cassert>
#include <cstdint>
#include <iostream>
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
  PrefetchRequest(champsim::address address, std::uint32_t confidenceLevel, std::uint64_t addedCycle) noexcept
      : address_(address), addedCycle_(addedCycle)
  {
    if (confidenceLevel > parameters::MAXIMUM_CONFIDENCE_LEVEL) {
      std::cerr << "Assertion failed: confidenceLevel (" << confidenceLevel
                << ") > MAXIMUM_CONFIDENCE_LEVEL (" << parameters::MAXIMUM_CONFIDENCE_LEVEL << ")\n";
    }
    assert(confidenceLevel <= parameters::MAXIMUM_CONFIDENCE_LEVEL && "Higher than maximum confidence allows");

    this->confidence_ = static_cast<float>(confidenceLevel) / parameters::MAXIMUM_CONFIDENCE_LEVEL;
  }

  // Return the delay from when this request was added to when it was used
  std::uint64_t get_delay(std::uint64_t cycle) noexcept
  {
    assert(cycle >= addedCycle_ && "Request is being used before it became ready");
    return cycle - addedCycle_;
  }

  float get_confidence() const noexcept {
    return confidence_;
  }

  // Compare by block number instead of full address
  bool operator==(const PrefetchRequest& rhs) const noexcept { 
    return champsim::block_number{address_} == champsim::block_number{rhs.address_}; 
  }

  bool operator!=(const PrefetchRequest& rhs) const noexcept { 
    return !(*this == rhs); 
  }

private:
  champsim::address address_;   // Memory address for this request
  std::uint64_t addedCycle_;    // Cycle when this request becomes ready
  float confidence_;
};

using PrefetchRequestPtr = std::shared_ptr<PrefetchRequest>;

} // namespace dram_open