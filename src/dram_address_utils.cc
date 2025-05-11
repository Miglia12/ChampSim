#include "dram_prefetches_scheduler/dram_address_utils.h"

namespace dram_open
{
DramAddressFunction get_channel_func = nullptr;
DramAddressFunction get_rank_func = nullptr;
DramAddressFunction get_bankgroup_func = nullptr;
DramAddressFunction get_bank_func = nullptr;
DramAddressFunction get_row_func = nullptr;
DramAddressFunction get_column_func = nullptr;
} // namespace dram_open