#pragma once

#include <cassert>
#include <cstdint>
#include <string>
#include <vector>

#include "scheduler_parameters.h"

namespace dram_open
{

// Structure representing a single histogram bucket
struct HistogramBucket {
  std::uint64_t minValue;   // Minimum value in this bucket (inclusive)
  std::uint64_t maxValue;   // Maximum value in this bucket (inclusive, UINT64_MAX for last bucket)
  std::uint64_t rowCount;   // Number of rows in this bucket
  std::uint64_t totalValue; // Sum of all values in this bucket

  double getAverageValue() const noexcept { return rowCount > 0 ? static_cast<double>(totalValue) / rowCount : 0.0; }

  std::string getRangeString() const
  {
    if (minValue == 0 && maxValue == 0) {
      return "0";
    } else if (minValue == maxValue) {
      return std::to_string(minValue);
    } else if (maxValue == UINT64_MAX) {
      return std::to_string(minValue) + "+";
    } else {
      return std::to_string(minValue) + "-" + std::to_string(maxValue);
    }
  }
};

// Power-of-2 histogram with fixed bucket boundaries
class PowerOfTwoHistogram
{
private:
  std::vector<HistogramBucket> buckets;

public:
  PowerOfTwoHistogram()
  {
    // Total buckets = 1 (for zero) + HISTOGRAM_BUCKETS
    buckets.resize(parameters::HISTOGRAM_BUCKETS + 1);

    // Bucket 0 is always for value 0
    buckets[0].minValue = 0;
    buckets[0].maxValue = 0;
    buckets[0].rowCount = 0;
    buckets[0].totalValue = 0;

    // Remaining buckets use power-of-2 boundaries
    for (std::size_t i = 1; i <= parameters::HISTOGRAM_BUCKETS; ++i) {
      std::uint64_t powerOfTwo = 1ULL << (i - 1); // 1, 2, 4, 8, 16, ...

      buckets[i].minValue = powerOfTwo;

      // Last bucket goes to infinity
      if (i == parameters::HISTOGRAM_BUCKETS) {
        buckets[i].maxValue = UINT64_MAX;
      } else {
        buckets[i].maxValue = (1ULL << i) - 1; // 1, 3, 7, 15, 31, ...
      }

      buckets[i].rowCount = 0;
      buckets[i].totalValue = 0;
    }
  }

  void addValue(std::uint64_t value)
  {
    std::size_t bucketIndex = getBucketIndex(value);
    buckets[bucketIndex].rowCount++;
    buckets[bucketIndex].totalValue += value;
  }

  const std::vector<HistogramBucket>& getBuckets() const { return buckets; }

  std::uint64_t getTotalRows() const
  {
    std::uint64_t total = 0;
    for (const auto& bucket : buckets) {
      total += bucket.rowCount;
    }
    return total;
  }

  std::uint64_t getTotalValue() const
  {
    std::uint64_t total = 0;
    for (const auto& bucket : buckets) {
      total += bucket.totalValue;
    }
    return total;
  }

private:
  std::size_t getBucketIndex(std::uint64_t value) const
  {
    if (value == 0) {
      return 0;
    }

    // Find the highest bit set
    std::size_t highestBit = 0;
    std::uint64_t temp = value;
    while (temp >>= 1) {
      highestBit++;
    }

    // Bucket index is highestBit + 1 (since bucket 0 is for value 0)
    std::size_t bucketIndex = highestBit + 1;

    // Clamp to maximum bucket index
    if (bucketIndex > parameters::HISTOGRAM_BUCKETS) {
      bucketIndex = parameters::HISTOGRAM_BUCKETS;
    }

    return bucketIndex;
  }
};

// Container for both open and access histograms
struct RowAccessHistogram {
  PowerOfTwoHistogram openHistogram;
  PowerOfTwoHistogram accessHistogram;

  std::uint64_t getTotalUniqueRows() const
  {
    assert(openHistogram.getTotalRows() == accessHistogram.getTotalRows() && "The size of the two istograms should be the same");
    return openHistogram.getTotalRows();
  }

  std::uint64_t getTotalOpens() const { return openHistogram.getTotalValue(); }

  std::uint64_t getTotalAccesses() const { return accessHistogram.getTotalValue(); }
};

} // namespace dram_open