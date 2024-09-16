/*
 *    Copyright 2023 The ChampSim Contributors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "dram_controller.h"

#include <algorithm>
#include <cfenv>
#include <cmath>
#include <fmt/core.h>

#include "deadlock.h"
#include "instruction.h"
#include "util/bits.h" // for lg2, bitmask
#include "util/span.h"
#include "util/units.h"

MEMORY_CONTROLLER::MEMORY_CONTROLLER(champsim::chrono::picoseconds clock_period_, champsim::chrono::picoseconds t_rp, champsim::chrono::picoseconds t_rcd,
                                     champsim::chrono::picoseconds t_cas, champsim::chrono::picoseconds turnaround, std::vector<channel_type*>&& ul,
                                     std::size_t rq_size, std::size_t wq_size, std::size_t chans, champsim::data::bytes chan_width, std::size_t pref_size,
                                     std::size_t rows, std::size_t columns, std::size_t ranks, std::size_t banks)
    : champsim::operable(clock_period_), queues(std::move(ul)),  channel_width(chan_width), address_mapping(chan_width,pref_size,chans,banks,columns,ranks,rows)
{
  for (std::size_t i{0}; i < chans; ++i) {
    channels.emplace_back(clock_period_, t_rp, t_rcd, t_cas, turnaround, chan_width, rq_size, wq_size, address_mapping);
  }
}

DRAM_CHANNEL::DRAM_CHANNEL(champsim::chrono::picoseconds clock_period_, champsim::chrono::picoseconds t_rp, champsim::chrono::picoseconds t_rcd,
                           champsim::chrono::picoseconds t_cas, champsim::chrono::picoseconds turnaround, champsim::data::bytes width, std::size_t rq_size,
                           std::size_t wq_size, DRAM_ADDRESS_MAPPING addr_mapper)
    : champsim::operable(clock_period_), address_mapping(addr_mapper), WQ(wq_size,(address_mapping.prefetch_size * width.count())/BLOCK_SIZE), RQ(rq_size,(address_mapping.prefetch_size * width.count())/BLOCK_SIZE), channel_width(width), tRP(t_rp), tRCD(t_rcd), tCAS(t_cas),
      DRAM_DBUS_TURN_AROUND_TIME(turnaround),
      DRAM_DBUS_RETURN_TIME(std::chrono::duration_cast<champsim::chrono::clock::duration>(clock_period_ * address_mapping.prefetch_size))
{
  request_array_type br(address_mapping.ranks() * address_mapping.banks());
  bank_request = br;
}

DRAM_ADDRESS_MAPPING::DRAM_ADDRESS_MAPPING(champsim::data::bytes channel_width, std::size_t pref_size, std::size_t channels, std::size_t banks, std::size_t columns, std::size_t ranks, std::size_t rows):
 address_slicer(make_slicer(channel_width,pref_size,channels,banks,columns,ranks,rows)), prefetch_size(pref_size)
 {
  //assert prefetch size is not zero
  assert(prefetch_size != 0);
  //assert prefetch size is multiple of block size
  assert((channel_width.count() * prefetch_size) % BLOCK_SIZE == 0);
 }

auto DRAM_ADDRESS_MAPPING::make_slicer(champsim::data::bytes channel_width, std::size_t pref_size, std::size_t channels, std::size_t banks, std::size_t columns, std::size_t ranks, std::size_t rows) -> slicer_type
{
  std::array<std::size_t, slicer_type::size()> params{};
  params.at(SLICER_ROW_IDX) = rows;
  params.at(SLICER_COLUMN_IDX) = columns / pref_size;
  params.at(SLICER_RANK_IDX) = ranks;
  params.at(SLICER_BANK_IDX) = banks;
  params.at(SLICER_CHANNEL_IDX) = channels;
  params.at(SLICER_OFFSET_IDX) = channel_width.count() * pref_size;
  return std::apply([start = 0](auto... p) { return champsim::make_contiguous_extent_set(start, champsim::lg2(p)...); }, params);
}

long MEMORY_CONTROLLER::operate()
{
  long progress{0};

  initiate_requests();

  for (auto& channel : channels) {
    progress += channel._operate();
  }

  return progress;
}

long DRAM_CHANNEL::operate()
{
  long progress{0};

  if (warmup) {
    for (auto& entry : RQ) {
      if (entry.valid) {

        for (auto& pkt : entry.packets){
          if(pkt.has_value())
          {
            response_type response{pkt.value().address, pkt.value().v_address, pkt.value().data,
                              pkt.value().pf_metadata, pkt.value().instr_depend_on_me};
            for (auto* ret : pkt.value().to_return) {
              ret->push_back(response);
            }
            pkt.reset();
          }
        }

        ++progress;
        entry.valid = false;
      }
    }

    for (auto& entry : WQ) {
      if (entry.valid) {
        ++progress;
      }
      for (auto& pkt : entry.packets)
        if(pkt.has_value())
          pkt.reset();
      entry.valid = false;
    }
  }

  check_write_collision();
  check_read_collision();
  progress += finish_dbus_request();
  swap_write_mode();
  progress += populate_dbus();
  progress += schedule_packets();

  return progress;
}

long DRAM_CHANNEL::finish_dbus_request()
{
  long progress{0};

  if (active_request != std::end(bank_request) && active_request->ready_time <= current_time) {
    for (auto& pkt : active_request->pkt->packets){
      if(pkt.has_value())
      {
        response_type response{pkt.value().address, pkt.value().v_address, pkt.value().data,
                              pkt.value().pf_metadata, pkt.value().instr_depend_on_me};
        for (auto* ret : pkt.value().to_return) {
          ret->push_back(response);
        }
        pkt.reset();
      }
    }
    active_request->valid = false;
    active_request->pkt->valid = false;
    active_request = std::end(bank_request);
    ++progress;
  }

  return progress;
}

void DRAM_CHANNEL::swap_write_mode()
{
  // these values control when to send out a burst of writes
  const std::size_t DRAM_WRITE_HIGH_WM = ((std::size(WQ) * 7) >> 3); // 7/8th
  const std::size_t DRAM_WRITE_LOW_WM = ((std::size(WQ) * 6) >> 3);  // 6/8th
  // const std::size_t MIN_DRAM_WRITES_PER_SWITCH = ((std::size(WQ) * 1) >> 2); // 1/4

  // Check queue occupancy
  auto wq_occu = static_cast<std::size_t>(std::count_if(std::begin(WQ), std::end(WQ), [](const auto& x) { return x.valid; }));
  auto rq_occu = static_cast<std::size_t>(std::count_if(std::begin(RQ), std::end(RQ), [](const auto& x) { return x.valid; }));

  // Change modes if the queues are unbalanced
  if ((!write_mode && (wq_occu >= DRAM_WRITE_HIGH_WM || (rq_occu == 0 && wq_occu > 0)))
      || (write_mode && (wq_occu == 0 || (rq_occu > 0 && wq_occu < DRAM_WRITE_LOW_WM)))) {
    // Reset scheduled requests
    for (auto it = std::begin(bank_request); it != std::end(bank_request); ++it) {
      // Leave active request on the data bus
      if (it != active_request && it->valid) {
        // Leave rows charged
        if (it->ready_time < (current_time + tCAS)) {
          it->open_row.reset();
        }

        // This bank is ready for another DRAM request
        it->valid = false;
        it->pkt->scheduled = false;
        it->pkt->ready_time = current_time;
      }
    }

    // Add data bus turn-around time
    if (active_request != std::end(bank_request)) {
      dbus_cycle_available = active_request->ready_time + DRAM_DBUS_TURN_AROUND_TIME; // After ongoing finish
    } else {
      dbus_cycle_available = current_time + DRAM_DBUS_TURN_AROUND_TIME;
    }

    // Invert the mode
    write_mode = !write_mode;
  }
}

// Look for requests to put on the bus
long DRAM_CHANNEL::populate_dbus()
{
  long progress{0};

  auto iter_next_process = std::min_element(std::begin(bank_request), std::end(bank_request),
                                            [](const auto& lhs, const auto& rhs) { return !rhs.valid || (lhs.valid && lhs.ready_time < rhs.ready_time); });
  if (iter_next_process->valid && iter_next_process->ready_time <= current_time) {
    if (active_request == std::end(bank_request) && dbus_cycle_available <= current_time) {
      // Bus is available
      // Put this request on the data bus
      active_request = iter_next_process;
      active_request->ready_time = current_time + DRAM_DBUS_RETURN_TIME;
      if (iter_next_process->row_buffer_hit) {
        if (write_mode) {
          ++sim_stats.WQ_ROW_BUFFER_HIT;
        } else {
          ++sim_stats.RQ_ROW_BUFFER_HIT;
        }
      } else if (write_mode) {
        ++sim_stats.WQ_ROW_BUFFER_MISS;
      } else {
        ++sim_stats.RQ_ROW_BUFFER_MISS;
      }

      ++progress;
    } else {
      // Bus is congested
      if (active_request != std::end(bank_request)) {
        sim_stats.dbus_cycle_congested += (active_request->ready_time - current_time) / clock_period;
      } else {
        sim_stats.dbus_cycle_congested += (dbus_cycle_available - current_time) / clock_period;
      }
      ++sim_stats.dbus_count_congested;
    }
  }

  return progress;
}

std::size_t DRAM_CHANNEL::bank_request_index(champsim::address addr) const
{
  auto op_rank = address_mapping.get_rank(addr);
  auto op_bank = address_mapping.get_bank(addr);
  return op_rank * address_mapping.banks() + op_bank;
}

// Look for queued packets that have not been scheduled
long DRAM_CHANNEL::schedule_packets()
{
  long progress{0};

  // Look for queued packets that have not been scheduled
  // prioritize packets that are ready to execute, bank is free
  auto next_schedule = [this](const auto& lhs, const auto& rhs) {
    if (!(rhs.valid && !rhs.scheduled)) {
      return true;
    }
    if (!(lhs.valid && !lhs.scheduled)) {
      return false;
    }

    auto lop_idx = this->bank_request_index(lhs.address);
    auto rop_idx = this->bank_request_index(rhs.address);
    auto rready = !this->bank_request[rop_idx].valid;
    auto lready = !this->bank_request[lop_idx].valid;
    return (rready && lready) ? lhs.ready_time <= rhs.ready_time : lready;
  };
  queue_type::iterator iter_next_schedule;
  if (write_mode) {
    iter_next_schedule = std::min_element(std::begin(WQ), std::end(WQ), next_schedule);
  } else {
    iter_next_schedule = std::min_element(std::begin(RQ), std::end(RQ), next_schedule);
  }

  if (iter_next_schedule->valid && iter_next_schedule->ready_time <= current_time) {
    auto op_row = address_mapping.get_row(iter_next_schedule->address);
    auto op_idx = bank_request_index(iter_next_schedule->address);

    if (!bank_request[op_idx].valid) {
      bool row_buffer_hit = (bank_request[op_idx].open_row.has_value() && *(bank_request[op_idx].open_row) == op_row);

      // this bank is now busy
      bank_request[op_idx] = {true, row_buffer_hit, std::optional{op_row},
                              current_time + tCAS + (row_buffer_hit ? champsim::chrono::clock::duration{} : tRP + tRCD), iter_next_schedule};

      iter_next_schedule->scheduled = true;
      iter_next_schedule->ready_time = champsim::chrono::clock::time_point::max();

      ++progress;
    }
  }

  return progress;
}

void MEMORY_CONTROLLER::initialize()
{
  using namespace champsim::data::data_literals;
  using namespace std::literals::chrono_literals;
  auto sz = this->size();
  if (champsim::data::gibibytes gb_sz{sz}; gb_sz > 1_GiB) {
    fmt::print("Off-chip DRAM Size: {}", gb_sz);
  } else if (champsim::data::mebibytes mb_sz{sz}; mb_sz > 1_MiB) {
    fmt::print("Off-chip DRAM Size: {}", mb_sz);
  } else if (champsim::data::kibibytes kb_sz{sz}; kb_sz > 1_kiB) {
    fmt::print("Off-chip DRAM Size: {}", kb_sz);
  } else {
    fmt::print("Off-chip DRAM Size: {}", sz);
  }
  fmt::print(" Channels: {} Width: {}-bit Data Rate: {} MT/s\n", std::size(channels), champsim::data::bits_per_byte * channel_width.count(),
             1us / clock_period);
}

void DRAM_CHANNEL::initialize() {}

void MEMORY_CONTROLLER::begin_phase()
{
  std::size_t chan_idx = 0;
  for (auto& chan : channels) {
    DRAM_CHANNEL::stats_type new_stats;
    new_stats.name = "Channel " + std::to_string(chan_idx++);
    chan.sim_stats = new_stats;
    chan.warmup = warmup;
  }

  for (auto* ul : queues) {
    channel_type::stats_type ul_new_roi_stats;
    channel_type::stats_type ul_new_sim_stats;
    ul->roi_stats = ul_new_roi_stats;
    ul->sim_stats = ul_new_sim_stats;
  }
}

void DRAM_CHANNEL::begin_phase() {}

void MEMORY_CONTROLLER::end_phase(unsigned cpu)
{
  for (auto& chan : channels) {
    chan.end_phase(cpu);
  }
}

void DRAM_CHANNEL::end_phase(unsigned /*cpu*/) { roi_stats = sim_stats; }

bool DRAM_ADDRESS_MAPPING::is_collision(champsim::address a, champsim::address b) const
{
  //collision if everything but offset matches
  champsim::data::bits offset_bits = champsim::data::bits{champsim::size(get<SLICER_OFFSET_IDX>(address_slicer))};
  return(a.slice_upper(offset_bits) == b.slice_upper(offset_bits));
}

void DRAM_CHANNEL::check_write_collision()
{
  for (auto wq_it = std::begin(WQ); wq_it != std::end(WQ); ++wq_it) {
    if (wq_it->valid && !wq_it->forward_checked) {
      auto checker = [chan = this, check_val = wq_it->address](const auto& pkt) {
        return pkt.valid && chan->address_mapping.is_collision(pkt.address,check_val);
      };

      auto found = std::find_if(std::begin(WQ), wq_it, checker); // Forward check
      if (found == wq_it) {
        found = std::find_if(std::next(wq_it), std::end(WQ), checker); // Backward check
      }

      if (found != std::end(WQ)) {
        wq_it->valid = false;
        //reset the packet entries
        for(auto& pkt : wq_it->packets)
          if(pkt.has_value())
            pkt.reset();
      } else {
        wq_it->forward_checked = true;
      }
    }
  }
}

void DRAM_CHANNEL::check_read_collision()
{
  for (auto rq_it = std::begin(RQ); rq_it != std::end(RQ); ++rq_it) {
    if (rq_it->valid && !rq_it->forward_checked) {
      auto checker = [chan = this, check_val = rq_it->address](const auto& x) {
        return x.valid && chan->address_mapping.is_collision(x.address, check_val);
      };
      //check for write forwarding
      if (auto wq_it = std::find_if(std::begin(WQ), std::end(WQ), checker); wq_it != std::end(WQ)) {
        for(auto& pkt : rq_it->packets)
        {
          if(pkt.has_value())
          {
            response_type response{pkt.value().address, pkt.value().v_address, pkt.value().data, pkt.value().pf_metadata,
                                pkt.value().instr_depend_on_me};
            for (auto* ret : pkt.value().to_return) {
              ret->push_back(response);
            }
            pkt.reset();
          }
        }

        rq_it->valid = false;
      
      //backwards check
      } else if (auto found = std::find_if(std::begin(RQ), rq_it, checker); found != rq_it) {
        //merge packets
        for(auto& mrg_pkt : rq_it->packets) {
          if(mrg_pkt.has_value())
          {
            auto mrg_into_pkt = std::find_if(std::begin(found->packets), std::end(found->packets), [mrg_pkt = mrg_pkt](const auto& pkt) { return pkt.has_value() && mrg_pkt.value().address == pkt.value().address;});
            //found two packets with the same address
            if(mrg_into_pkt != std::end(found->packets))
            {
              //merge instr and to-returns
              auto instr_copy = std::move(mrg_into_pkt->value().instr_depend_on_me);
              auto ret_copy = std::move(mrg_into_pkt->value().to_return);
              std::set_union(std::begin(instr_copy), std::end(instr_copy), std::begin(mrg_pkt.value().instr_depend_on_me), std::end(mrg_pkt.value().instr_depend_on_me),
                        std::back_inserter(mrg_into_pkt->value().instr_depend_on_me));
              std::set_union(std::begin(ret_copy), std::end(ret_copy), std::begin(mrg_pkt.value().to_return), std::end(mrg_pkt.value().to_return),
                        std::back_inserter(mrg_into_pkt->value().to_return));
            }
            //add to packets list
            else
            {
              //find an empty slot and put it there
              auto empty_packet_slot = std::find_if(std::begin(found->packets), std::end(found->packets), [](const auto& pkt) {return !pkt.has_value();});
              //assert we have space
              assert(empty_packet_slot != std::end(found->packets));
              *empty_packet_slot = mrg_pkt.value();
            }
            mrg_pkt.reset();
          }
        }
        rq_it->valid = false;
      //forwards check
      } else if (found = std::find_if(std::next(rq_it), std::end(RQ), checker); found != std::end(RQ)) {
        //merge packets
        for(auto& mrg_pkt : rq_it->packets) {
          if(mrg_pkt.has_value())
          {
            auto mrg_into_pkt = std::find_if(std::begin(found->packets), std::end(found->packets), [mrg_pkt = mrg_pkt](const auto& pkt) { return pkt.has_value() && mrg_pkt.value().address == pkt.value().address;});
            //found two packets with the same address
            if(mrg_into_pkt != std::end(found->packets))
            {
              //merge instr and to-returns
              auto instr_copy = std::move(mrg_into_pkt->value().instr_depend_on_me);
              auto ret_copy = std::move(mrg_into_pkt->value().to_return);
              std::set_union(std::begin(instr_copy), std::end(instr_copy), std::begin(mrg_pkt.value().instr_depend_on_me), std::end(mrg_pkt.value().instr_depend_on_me),
                        std::back_inserter(mrg_into_pkt->value().instr_depend_on_me));
              std::set_union(std::begin(ret_copy), std::end(ret_copy), std::begin(mrg_pkt.value().to_return), std::end(mrg_pkt.value().to_return),
                        std::back_inserter(mrg_into_pkt->value().to_return));

            }
            //add packets to list
            else
            {
              //find an empty slot and put it there
              auto empty_packet_slot = std::find_if(std::begin(found->packets),std::end(found->packets),[](const auto& pkt) {return !pkt.has_value();});
              assert(empty_packet_slot != std::end(found->packets));
              *empty_packet_slot = mrg_pkt.value();
            }
            mrg_pkt.reset();
          }
        }
        rq_it->valid = false;
      } else {
        rq_it->forward_checked = true;
      }
    }
  }
}

void MEMORY_CONTROLLER::initiate_requests()
{
  // Initiate read requests
  for (auto* ul : queues) {
    for (auto q : {std::ref(ul->RQ), std::ref(ul->PQ)}) {
      auto [begin, end] = champsim::get_span_p(std::cbegin(q.get()), std::cend(q.get()), [ul, this](const auto& pkt) { return this->add_rq(pkt, ul); });
      q.get().erase(begin, end);
    }

    // Initiate write requests
    auto [wq_begin, wq_end] = champsim::get_span_p(std::cbegin(ul->WQ), std::cend(ul->WQ), [this](const auto& pkt) { return this->add_wq(pkt); });
    ul->WQ.erase(wq_begin, wq_end);
  }
}

DRAM_CHANNEL::packet_type::packet_type(const typename champsim::channel::request_type& req)
      : pf_metadata(req.pf_metadata), address(req.address), v_address(req.address), data(req.data), instr_depend_on_me(req.instr_depend_on_me)
{
  asid[0] = req.asid[0];
  asid[1] = req.asid[1];
}
DRAM_CHANNEL::request_type::request_type(std::size_t packets_per_req):
packets{packets_per_req}
{}

bool MEMORY_CONTROLLER::add_rq(const request_type& packet, champsim::channel* ul)
{
  auto& channel = channels[address_mapping.get_channel(packet.address)];

  if (auto rq_it = std::find_if_not(std::begin(channel.RQ), std::end(channel.RQ), [this](const auto& pkt) { return pkt.valid; });
      rq_it != std::end(channel.RQ)) {
      rq_it->packets[0] = DRAM_CHANNEL::packet_type{packet};
      rq_it->forward_checked = false;
      rq_it->scheduled = false;
      rq_it->ready_time = current_time;
      if (packet.response_requested)
        rq_it->packets[0].value().to_return = {&ul->returned};
      rq_it->valid = true;

      rq_it->address = packet.address;

    return true;
  }

  return false;
}

bool MEMORY_CONTROLLER::add_wq(const request_type& packet)
{
  auto& channel = channels[address_mapping.get_channel(packet.address)];

  // search for the empty index
  if (auto wq_it = std::find_if_not(std::begin(channel.WQ), std::end(channel.WQ), [this](const auto& pkt) { return pkt.valid; });
      wq_it != std::end(channel.WQ)) {
      wq_it->packets[0] = DRAM_CHANNEL::packet_type{packet};
      wq_it->forward_checked = false;
      wq_it->scheduled = false;
      wq_it->ready_time = current_time;
      wq_it->valid = true;

      wq_it->address = packet.address;

    return true;
  }

  ++channel.sim_stats.WQ_FULL;
  return false;
}

unsigned long DRAM_ADDRESS_MAPPING::get_channel(champsim::address address) const { return std::get<SLICER_CHANNEL_IDX>(address_slicer(address)).to<unsigned long>(); } 
unsigned long DRAM_ADDRESS_MAPPING::get_rank(champsim::address address) const { return std::get<SLICER_RANK_IDX>(address_slicer(address)).to<unsigned long>(); }
unsigned long DRAM_ADDRESS_MAPPING::get_bank(champsim::address address) const { return std::get<SLICER_BANK_IDX>(address_slicer(address)).to<unsigned long>(); }
unsigned long DRAM_ADDRESS_MAPPING::get_row(champsim::address address) const { return std::get<SLICER_ROW_IDX>(address_slicer(address)).to<unsigned long>(); }
unsigned long DRAM_ADDRESS_MAPPING::get_column(champsim::address address) const { return std::get<SLICER_COLUMN_IDX>(address_slicer(address)).to<unsigned long>(); }
unsigned long DRAM_ADDRESS_MAPPING::get_offset(champsim::address address) const { return std::get<SLICER_OFFSET_IDX>(address_slicer(address)).to<unsigned long>(); }

champsim::data::bytes MEMORY_CONTROLLER::size() const
{
  return champsim::data::bytes{(1ll << address_mapping.address_slicer.bit_size())};
}

std::size_t DRAM_ADDRESS_MAPPING::rows() const { return std::size_t{1} << champsim::size(get<SLICER_ROW_IDX>(address_slicer)); }
std::size_t DRAM_ADDRESS_MAPPING::columns() const { return prefetch_size << champsim::size(get<SLICER_COLUMN_IDX>(address_slicer)); }
std::size_t DRAM_ADDRESS_MAPPING::ranks() const { return std::size_t{1} << champsim::size(get<SLICER_RANK_IDX>(address_slicer)); }
std::size_t DRAM_ADDRESS_MAPPING::banks() const { return std::size_t{1} << champsim::size(get<SLICER_BANK_IDX>(address_slicer)); }
std::size_t DRAM_ADDRESS_MAPPING::channels() const { return std::size_t{1} << champsim::size(get<SLICER_CHANNEL_IDX>(address_slicer)); }
std::size_t DRAM_ADDRESS_MAPPING::offset() const { return std::size_t{1} << champsim::size(get<SLICER_OFFSET_IDX>(address_slicer)); };
std::size_t DRAM_CHANNEL::bank_request_capacity() const { return std::size(bank_request); }

// LCOV_EXCL_START Exclude the following function from LCOV
void MEMORY_CONTROLLER::print_deadlock()
{
  int j = 0;
  for (auto& chan : channels) {
    fmt::print("DRAM Channel {}\n", j++);
    chan.print_deadlock();
  }
}

void DRAM_CHANNEL::print_deadlock()
{
  std::string_view q_writer{"valid: {} address: {} packets: {} forward_checked: {} scheduled: {}"};
  auto q_entry_pack = [](const auto& entry) {

    std::size_t valid_packets = std::count_if(std::begin(entry.packets),std::end(entry.packets), [](const auto& pkt) {return pkt.has_value();});
    return std::tuple{entry.valid, entry.address,valid_packets, entry.forward_checked , entry.scheduled};
  };

  champsim::range_print_deadlock(RQ, "RQ", q_writer, q_entry_pack);
  champsim::range_print_deadlock(WQ, "WQ", q_writer, q_entry_pack);
}
// LCOV_EXCL_STOP
