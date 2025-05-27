#include "ip_stride_tri.h"

#include "cache.h"

void ip_stride_tri::prefetcher_initialize()
{
  std::cout << "IP-Stride Prefetcher with DRAM Row Warming" << std::endl;
  std::cout << "Using confidence level " << DEFAULT_CONFIDENCE << " for DRAM row opening" << std::endl;
}

bool ip_stride_tri::submit_dram_row_open(champsim::address addr, uint32_t confidence, uint32_t metadata)
{
  // Call internal submit_dram_row_open method from the cache to submit the request
  bool success = intern_->submit_dram_request(addr, confidence, 0);

  if (success) {
    dram_row_warm_issued++;
  }

  return success;
}

void ip_stride_tri::setup_extended_warming(champsim::address addr, champsim::address::difference_type stride)
{
  // Always update extended warming to match normal prefetching behavior
  extended_warm_lookahead = {addr, stride, WARM_DEGREE};
  dram_warm_extended++;
}

uint32_t ip_stride_tri::prefetcher_cache_operate(champsim::address addr, champsim::address ip, uint8_t cache_hit, bool useful_prefetch, access_type type,
                                                 uint32_t metadata_in)
{
  champsim::block_number cl_addr{addr};
  champsim::block_number::difference_type stride = 0;

  auto found = table.check_hit({ip, cl_addr, stride});

  // if we found a matching entry
  if (found.has_value()) {
    stride = champsim::offset(found->last_cl_addr, cl_addr);

    // Initialize prefetch state unless we somehow saw the same address twice in
    // a row or if this is the first time we've seen this stride
    if (stride != 0 && stride == found->last_stride) {
      active_lookahead = {champsim::address{cl_addr}, stride, PREFETCH_DEGREE};

      // Setup extended warming (beyond prefetch distance)
      setup_extended_warming(champsim::address{cl_addr}, stride);
    }
  }

  // update tracking set
  table.fill({ip, cl_addr, stride});

  return metadata_in;
}

void ip_stride_tri::prefetcher_cycle_operate()
{
  // Original IP Stride prefetching logic with immediate DRAM warming for special cases
  if (active_lookahead.has_value()) {
    auto [old_pf_address, stride, degree] = active_lookahead.value();
    assert(degree > 0);

    champsim::address pf_address{champsim::block_number{old_pf_address} + stride};

    // Check for page crossing
    bool crosses_page = champsim::page_number{pf_address} != champsim::page_number{old_pf_address};

    // If virtual prefetch enabled OR still on same page
    if (intern_->virtual_prefetch || !crosses_page) {
      // Check MSHR occupancy
      const bool mshr_under_light_load = intern_->get_mshr_occupancy_ratio() < 0.5;

      // Issue normal prefetch if MSHR has space
      if (mshr_under_light_load) {
        const bool success = prefetch_line(pf_address, true, 0);
        if (success)
          active_lookahead = {pf_address, stride, degree - 1};
        // If we fail, try again next cycle
      } else {
        // MSHR is too full for normal prefetch, issue DRAM warming IMMEDIATELY
        dram_warm_mshr_full++;
        dram_warm_requests++;

        // Submit DRAM row open instead of regular prefetch
        submit_dram_row_open(pf_address, DEFAULT_CONFIDENCE, 0);

        active_lookahead = {pf_address, stride, degree - 1};
      }
    } else {
      // Handle page crossing - IMMEDIATELY issue DRAM warming
      dram_warm_cross_page++;
      dram_warm_requests++;

      // Submit DRAM row open for the page-crossing address
      submit_dram_row_open(pf_address, DEFAULT_CONFIDENCE, 0);

      // Reset active_lookahead since we can't cross pages
      active_lookahead = {champsim::address{}, stride, 0};
    }

    if (active_lookahead->degree == 0) {
      active_lookahead.reset();
    }
  }

  // DRAM warming for extended distances (processed separately)
  if (extended_warm_lookahead.has_value()) {
    auto [base_address, stride, degree] = extended_warm_lookahead.value();
    assert(degree > 0);

    // Calculate address to warm - use PREFETCH_DEGREE+degree as offset
    int offset = PREFETCH_DEGREE + (WARM_DEGREE - degree) + 1;
    champsim::address warm_address{champsim::block_number{base_address} + (stride * offset)};

    // Only warm if we stay on the same page
    if (champsim::page_number{warm_address} == champsim::page_number{base_address}) {
      dram_warm_requests++;
      submit_dram_row_open(warm_address, DEFAULT_CONFIDENCE, 0);
    }

    // Move to next degree regardless of success
    extended_warm_lookahead = {base_address, stride, degree - 1};

    if (extended_warm_lookahead->degree == 0) {
      extended_warm_lookahead.reset();
    }
  }
}

uint32_t ip_stride_tri::prefetcher_cache_fill(champsim::address addr, long set, long way, uint8_t prefetch, champsim::address evicted_addr,
                                              uint32_t metadata_in)
{
  return metadata_in;
}

void ip_stride_tri::prefetcher_final_stats()
{
  std::cout << "IP_STRIDE_TRI DRAM_ROW_WARMING:";
  std::cout << " WARM_REQUESTS: " << dram_warm_requests;
  std::cout << " WARM_ISSUED: " << dram_row_warm_issued;
  std::cout << " WARM_MSHR_FULL: " << dram_warm_mshr_full;
  std::cout << " WARM_CROSS_PAGE: " << dram_warm_cross_page;
  std::cout << " WARM_EXTENDED: " << dram_warm_extended;
  std::cout << std::endl;
}