#ifndef _BERTI_TRI_PARAMETERS_H_
#define _BERTI_TRI_PARAMETERS_H_

/*
 * Berti-Tri: An extension of the Berti prefetcher with DRAM row warming
 * Based on Berti prefetcher from:
 * 55th ACM/IEEE International Conference on Microarchitecture (MICRO 2022)
 */

namespace berti_tri_params
{
/*****************************************************************************
 *                              SIZES                                        *
 *****************************************************************************/
// BERTI
#define BERTI_TABLE_SIZE (64) // Big enough to fit all entries
#define BERTI_TABLE_DELTA_SIZE (16)

// HISTORY
#define HISTORY_TABLE_SETS (8)
#define HISTORY_TABLE_WAYS (16)

// Hash Function
// #define HASH_FN
// #define HASH_ORIGINAL
// #define THOMAS_WANG_HASH_1
// #define THOMAS_WANG_HASH_2
// #define THOMAS_WANG_HASH_3
// #define THOMAS_WANG_HASH_4
// #define THOMAS_WANG_HASH_5
// #define THOMAS_WANG_HASH_6
// #define THOMAS_WANG_HASH_7
// #define THOMAS_WANG_NEW_HASH
// #define THOMAS_WANG_HASH_HALF_AVALANCHE
// #define THOMAS_WANG_HASH_FULL_AVALANCHE
// #define THOMAS_WANG_HASH_INT_1
// #define THOMAS_WANG_HASH_INT_2
#define ENTANGLING_HASH
// #define FOLD_HASH

/*****************************************************************************
 *                              MASKS                                        *
 *****************************************************************************/
#define SIZE_IP_MASK (64)
#define IP_MASK (0xFFFF)
#define TIME_MASK (0xFFFF)
#define LAT_MASK (0xFFF)
#define ADDR_MASK (0xFFFFFF)
#define DELTA_MASK (12)
#define TABLE_SET_MASK (0x7)

/*****************************************************************************
 *                      CONFIDENCE VALUES                                    *
 *****************************************************************************/
#define CONFIDENCE_MAX (16) // 6 bits
#define CONFIDENCE_INC (1)  // 6 bits
#define CONFIDENCE_INIT (1) // 6 bits

#define CONFIDENCE_L1 (10) // 6 bits
#define CONFIDENCE_L2 (8)  // 6 bits
#define CONFIDENCE_L2R (6) // 6 bits

#define CONFIDENCE_MIDDLE_L1 (14) // 6 bits
#define CONFIDENCE_MIDDLE_L2 (12) // 6 bits
#define LAUNCH_MIDDLE_CONF (8)

/*****************************************************************************
 *                              LIMITS                                       *
 *****************************************************************************/
#define MAX_HISTORY_IP (8)
#define MSHR_LIMIT (70)

/*****************************************************************************
 *                              CONSTANT PARAMETERS                          *
 *****************************************************************************/
#define BERTI_R (0x0)
#define BERTI_L1 (0x1)
#define BERTI_L2 (0x2)
#define BERTI_L2R (0x3)

/*****************************************************************************
 *                      DRAM Row Warming Parameters                          *
 *****************************************************************************/

// DRAM row scheduler configuration
#define DRAM_SCHEDULER_QUEUE_SIZE (64)     // Maximum number of requests in scheduler
#define DRAM_SCHEDULER_READY_THRESHOLD (25) // Cycles before a request is considered ready
#define DRAM_SCHEDULER_SLACK (3)          // Additional cycles before pruning

// Fine-grained confidence levels for DRAM warming (inclusive)
// Note: These can be any values between 0 and CONFIDENCE_MAX (16)
#define DRAM_WARM_MIN_CONFIDENCE (5) // Minimum confidence for DRAM warming
#define DRAM_WARM_MAX_CONFIDENCE (6) // Maximum confidence for DRAM warming

// Bandwidth control parameters
#define DRAM_WARM_MAX_FRACTION (0.5) // Maximum fraction of PQ bandwidth to use

#define LATENCY_FACTOR_VALUE (0.2) // Reduce berti latency factor

} // namespace berti_tri_params

#endif /* _BERTI_TRI_PARAMETERS_H_ */