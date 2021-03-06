/* Copyright 2013-present Barefoot Networks, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 * Antonin Bas (antonin@barefootnetworks.com)
 *
 */

#include <bm/bm_sim/parser.h>
#include <bm/bm_sim/tables.h>
#include <bm/bm_sim/logger.h>

#include <unistd.h>
#include <time.h>
#include <tuple>
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <map>
#include <utility>
#include <string.h>
#include <random>

#include "simple_switch.h"

namespace {

struct hash_ex {
  uint32_t operator()(const char *buf, size_t s) const {
    const uint32_t p = 16777619;
    uint32_t hash = 2166136261;

    for (size_t i = 0; i < s; i++)
      hash = (hash ^ buf[i]) * p;

    hash += hash << 13;
    hash ^= hash >> 7;
    hash += hash << 3;
    hash ^= hash >> 17;
    hash += hash << 5;
    return static_cast<uint32_t>(hash);
  }
};

struct bmv2_hash {
  uint64_t operator()(const char *buf, size_t s) const {
    return bm::hash::xxh64(buf, s);
  }
};

struct multi_path_simple_hash {
  uint32_t operator()(const char *buf, size_t s) const {
    uint32_t hash = 0;

    for (size_t i = 0; i < s; i++) {
      hash = hash | buf[i];
    }
    return static_cast<uint32_t>(hash);
  }
};

uint32_t getProbabilisticSimpleSum(const char *buf, size_t max_paths) {
  uint32_t probability_sum = 0;
  for (size_t i = 15; i < max_paths + 15; i++) {
    probability_sum += 0 | buf[i];
  }
  return probability_sum;
}

struct cmp_str {
   bool operator()(char* a, char* b) const {
      return std::strcmp(a, b) < 0;
   }
};

std::map<char*, std::tuple<time_t, double, double>, cmp_str> maxflow_map;
double droppedAmount = 0;
double droppedPackets = 0;
double totalPassed = 0;
double currentSecond = 0;

double getPacketTotalLengthValue(char* packet_size) {
  long long packet_size0 = (unsigned char) packet_size[0];
  long long packet_size1 = (unsigned char) packet_size[1];
  return ((packet_size0 << 8) | packet_size1);
}

double getRandomDouble() {
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_real_distribution<> dis(0.0, 1.0);
  return dis(gen);
}

uint32_t getProbabilisticSimplePath(const char *buf, uint32_t max_paths, uint32_t random, char* route_id, char* packet_size) {
  uint32_t path = 0;
  uint32_t accum = 0.0;

  double drop_rate = std::get<2>(maxflow_map[route_id]);
  double drop_random = getRandomDouble();

  if (drop_rate && drop_random <= drop_rate) {
    droppedAmount += getPacketTotalLengthValue(packet_size);
    droppedPackets++;
    return max_paths;
  }
  totalPassed += getPacketTotalLengthValue(packet_size);

  for (size_t i = 15; i < max_paths + 15; i++) {
    accum += 0 | buf[i];
    if (random < accum) {
      path = static_cast<uint32_t>(i - 15);
      break;
    }
  }
  return path;
}

uint32_t getProbabilisticSimpleMaxPaths(const char *buf, size_t s) {
  uint32_t array_max = static_cast<uint32_t>(s - 15);
  uint32_t meta_max = static_cast<uint32_t>(0 | buf[14]);
  return array_max < meta_max ? array_max : meta_max;
}

double getMaxflowValue(char* maxflow_handle) {
  long long maxflow0 = (unsigned char) maxflow_handle[0];
  long long maxflow1 = (unsigned char) maxflow_handle[1];
  long long maxflow2 = (unsigned char) maxflow_handle[2];
  long long maxflow3 = (unsigned char) maxflow_handle[3];
  return (maxflow0 << 24) | (maxflow1 << 16) | (maxflow2 << 8) | maxflow3;
}

void computePacketTotalLength(char* route_id, char* packet_size) {
  double& total_length = std::get<1>(maxflow_map[route_id]);
  total_length += getPacketTotalLengthValue(packet_size);
}

double getPacketDropRate(char* maxflow_handle, char* route_id) {
  double maxflow = getMaxflowValue(maxflow_handle);
  double totalflow = std::get<1>(maxflow_map[route_id]) * 0.000008;
  return std::max(1.0 - (maxflow / totalflow), 0.0);
}

void computePacketDropRate(char* route_id, char* packet_size, char* maxflow_handle) {
  computePacketTotalLength(route_id, packet_size);
  // std::cout << getPacketTotalLengthValue(packet_size) << std::endl;
  if (difftime(time(0), std::get<0>(maxflow_map[route_id])) >= 1.0) {

    std::cout << currentSecond + 1 << ";"; // Seconds
    std::cout << getMaxflowValue(maxflow_handle) << ";"; // Maxflow
    std::cout << std::get<1>(maxflow_map[route_id]) << ";"; // Totalflow
    std::cout << std::get<2>(maxflow_map[route_id]) << ";"; // Drop rate:
    std::cout << droppedPackets << ";"; // Packets dropped
    std::cout << droppedAmount << ";"; // Total dropped
    std::cout << totalPassed << std::endl; // Total passed

    currentSecond++;
    droppedPackets = 0;
    droppedAmount = 0;
    totalPassed = 0;

    double drop_rate = getPacketDropRate(maxflow_handle, route_id);
    maxflow_map[route_id] = std::make_tuple(time(0), 0.0, drop_rate);
  }
}

void insertDefaultMaxflowEntry(char* route_id) {
  maxflow_map[route_id] = std::make_tuple(time(0), 0.0, 0.0);
}

void calculateDropRate(char* route_id, char* packet_size, char* maxflow_handle) {
  !maxflow_map.count(route_id) ?
    insertDefaultMaxflowEntry(route_id) :
    computePacketDropRate(route_id, packet_size, maxflow_handle);
}

struct probabilistic_simple_multipath {
  uint32_t operator()(const char *buf, size_t s) const {
    char route_id[8];
    char packet_size[2];
    char maxflow_handle[4];
    memcpy(route_id, buf, 8);
    memcpy(packet_size, buf + 8, 2);
    memcpy(maxflow_handle, buf + 10, 4);

    calculateDropRate(route_id, packet_size, maxflow_handle);
    uint32_t max_paths = getProbabilisticSimpleMaxPaths(buf, s);
    uint32_t probability_sum = getProbabilisticSimpleSum(buf, max_paths);
    uint32_t random = rand() % probability_sum;
    return getProbabilisticSimplePath(buf, max_paths, random, route_id, packet_size);
  }
};

std::map<std::string, std::pair<uint32_t, uint32_t>> round_map;

uint32_t get_deterministic_round_path(std::string route_id, uint32_t paths_number) {
  uint32_t default_route = 0;
  round_map[route_id] = !round_map.count(route_id) ?
    std::make_pair(default_route, paths_number) :
    std::make_pair((round_map[route_id].first + 1) % round_map[route_id].second, paths_number);
  return round_map[route_id].first;
}

struct deterministic_round_multipath {
  uint32_t operator()(const char *buf, size_t s) const {
    std::string route_id(buf, 8);
    uint32_t paths_number = 0 | buf[s - 1];
    return get_deterministic_round_path(route_id, paths_number);
  }
};

std::map<std::string, std::pair<uint32_t, uint32_t>> block_map;

uint32_t calculate_deterministic_block_path(const char *buf, std::string route_id, uint32_t max_paths) {
  uint32_t path = 0;
  uint32_t accum = 0;

  for (size_t i = 9; i < max_paths + 9; i++) {
    accum += 0 | buf[i];
    if (block_map[route_id].first < accum) {
      path = static_cast<uint32_t>(i - 9);
      break;
    }
  }
  return path;
}

uint32_t get_deterministic_blocks_num(const char *buf, uint32_t max_paths) {
  uint32_t accum = 0;
  for (size_t i = 9; i < max_paths + 9; i++) {
    accum += 0 | buf[i];
  }
  return accum;
}

uint32_t get_deterministic_block_path(const char *buf, std::string route_id, uint32_t max_paths) {
  uint32_t blocks_num = get_deterministic_blocks_num(buf, max_paths);
  block_map[route_id] = !block_map.count(route_id) ?
    std::make_pair(static_cast<uint32_t>(0), blocks_num) :
    std::make_pair((block_map[route_id].first + 1) % block_map[route_id].second, blocks_num);
  return calculate_deterministic_block_path(buf, route_id, max_paths);
}

uint32_t get_deterministic_block_max_paths(const char *buf, size_t s) {
  uint32_t array_max = static_cast<uint32_t>(s - 9);
  uint32_t meta_max = static_cast<uint32_t>(0 | buf[8]);
  return array_max < meta_max ? array_max : meta_max;
}

struct deterministic_block_multipath {
  uint32_t operator()(const char *buf, size_t s) const {
    std::string route_id(buf, 8);
    uint32_t max_paths = get_deterministic_block_max_paths(buf, s);
    return get_deterministic_block_path(buf, route_id, max_paths);
  }
};

}  // namespace

// if REGISTER_HASH calls placed in the anonymous namespace, some compiler can
// give an unused variable warning
REGISTER_HASH(hash_ex);
REGISTER_HASH(bmv2_hash);
REGISTER_HASH(multi_path_simple_hash);
REGISTER_HASH(probabilistic_simple_multipath);
REGISTER_HASH(deterministic_round_multipath);
REGISTER_HASH(deterministic_block_multipath);

extern int import_primitives();

SimpleSwitch::SimpleSwitch(int max_port, bool enable_swap)
  : Switch(enable_swap),
    max_port(max_port),
    input_buffer(1024),
#ifdef SSWITCH_PRIORITY_QUEUEING_ON
    egress_buffers(max_port, nb_egress_threads,
                   64, EgressThreadMapper(nb_egress_threads),
                   SSWITCH_PRIORITY_QUEUEING_NB_QUEUES),
#else
    egress_buffers(max_port, nb_egress_threads,
                   64, EgressThreadMapper(nb_egress_threads)),
#endif
    output_buffer(128),
    pre(new McSimplePreLAG()),
    start(clock::now()) {
  add_component<McSimplePreLAG>(pre);

  add_required_field("standard_metadata", "ingress_port");
  add_required_field("standard_metadata", "packet_length");
  add_required_field("standard_metadata", "instance_type");
  add_required_field("standard_metadata", "egress_spec");
  add_required_field("standard_metadata", "clone_spec");
  add_required_field("standard_metadata", "egress_port");

  force_arith_field("standard_metadata", "ingress_port");
  force_arith_field("standard_metadata", "packet_length");
  force_arith_field("standard_metadata", "instance_type");
  force_arith_field("standard_metadata", "egress_spec");
  force_arith_field("standard_metadata", "clone_spec");

  force_arith_field("queueing_metadata", "enq_timestamp");
  force_arith_field("queueing_metadata", "enq_qdepth");
  force_arith_field("queueing_metadata", "deq_timedelta");
  force_arith_field("queueing_metadata", "deq_qdepth");

  force_arith_field("intrinsic_metadata", "ingress_global_timestamp");
  force_arith_field("intrinsic_metadata", "lf_field_list");
  force_arith_field("intrinsic_metadata", "mcast_grp");
  force_arith_field("intrinsic_metadata", "resubmit_flag");
  force_arith_field("intrinsic_metadata", "egress_rid");
  force_arith_field("intrinsic_metadata", "recirculate_flag");

  import_primitives();
}

#define PACKET_LENGTH_REG_IDX 0

int
SimpleSwitch::receive_(int port_num, const char *buffer, int len) {
  static int pkt_id = 0;

  // this is a good place to call this, because blocking this thread will not
  // block the processing of existing packet instances, which is a requirement
  if (do_swap() == 0) {
    check_queueing_metadata();
  }

  // we limit the packet buffer to original size + 512 bytes, which means we
  // cannot add more than 512 bytes of header data to the packet, which should
  // be more than enough
  auto packet = new_packet_ptr(port_num, pkt_id++, len,
                               bm::PacketBuffer(len + 512, buffer, len));

  BMELOG(packet_in, *packet);

  PHV *phv = packet->get_phv();
  // many current P4 programs assume this
  // it is also part of the original P4 spec
  phv->reset_metadata();

  // setting standard metadata

  phv->get_field("standard_metadata.ingress_port").set(port_num);
  // using packet register 0 to store length, this register will be updated for
  // each add_header / remove_header primitive call
  packet->set_register(PACKET_LENGTH_REG_IDX, len);
  phv->get_field("standard_metadata.packet_length").set(len);
  Field &f_instance_type = phv->get_field("standard_metadata.instance_type");
  f_instance_type.set(PKT_INSTANCE_TYPE_NORMAL);

  if (phv->has_field("intrinsic_metadata.ingress_global_timestamp")) {
    phv->get_field("intrinsic_metadata.ingress_global_timestamp")
        .set(get_ts().count());
  }

  input_buffer.push_front(std::move(packet));
  return 0;
}

void
SimpleSwitch::start_and_return_() {
  check_queueing_metadata();

  std::thread t1(&SimpleSwitch::ingress_thread, this);
  t1.detach();
  for (size_t i = 0; i < nb_egress_threads; i++) {
    std::thread t2(&SimpleSwitch::egress_thread, this, i);
    t2.detach();
  }
  std::thread t3(&SimpleSwitch::transmit_thread, this);
  t3.detach();
}

void
SimpleSwitch::reset_target_state_() {
  bm::Logger::get()->debug("Resetting simple_switch target-specific state");
  get_component<McSimplePreLAG>()->reset_state();
}

int
SimpleSwitch::set_egress_queue_depth(int port, const size_t depth_pkts) {
  egress_buffers.set_capacity(port, depth_pkts);
  return 0;
}

int
SimpleSwitch::set_all_egress_queue_depths(const size_t depth_pkts) {
  for (int i = 0; i < max_port; i++) {
    set_egress_queue_depth(i, depth_pkts);
  }
  return 0;
}

int
SimpleSwitch::set_egress_queue_rate(int port, const uint64_t rate_pps) {
  egress_buffers.set_rate(port, rate_pps);
  return 0;
}

int
SimpleSwitch::set_all_egress_queue_rates(const uint64_t rate_pps) {
  for (int i = 0; i < max_port; i++) {
    set_egress_queue_rate(i, rate_pps);
  }
  return 0;
}

uint64_t
SimpleSwitch::get_time_elapsed_us() const {
  return get_ts().count();
}

uint64_t
SimpleSwitch::get_time_since_epoch_us() const {
  auto tp = clock::now();
  return duration_cast<ts_res>(tp.time_since_epoch()).count();
}

void
SimpleSwitch::transmit_thread() {
  while (1) {
    std::unique_ptr<Packet> packet;
    output_buffer.pop_back(&packet);
    BMELOG(packet_out, *packet);
    BMLOG_DEBUG_PKT(*packet, "Transmitting packet of size {} out of port {}",
                    packet->get_data_size(), packet->get_egress_port());
    transmit_fn(packet->get_egress_port(),
                packet->data(), packet->get_data_size());
  }
}

ts_res
SimpleSwitch::get_ts() const {
  return duration_cast<ts_res>(clock::now() - start);
}

void
SimpleSwitch::enqueue(int egress_port, std::unique_ptr<Packet> &&packet) {
    packet->set_egress_port(egress_port);

    PHV *phv = packet->get_phv();

    if (with_queueing_metadata) {
      phv->get_field("queueing_metadata.enq_timestamp").set(get_ts().count());
      phv->get_field("queueing_metadata.enq_qdepth")
          .set(egress_buffers.size(egress_port));
    }

#ifdef SSWITCH_PRIORITY_QUEUEING_ON
    size_t priority =
        phv->get_field(SSWITCH_PRIORITY_QUEUEING_SRC).get<size_t>();
    if (priority >= SSWITCH_PRIORITY_QUEUEING_NB_QUEUES) {
      bm::Logger::get()->error("Priority out of range, dropping packet");
      return;
    }
    egress_buffers.push_front(
        egress_port, SSWITCH_PRIORITY_QUEUEING_NB_QUEUES - 1 - priority,
        std::move(packet));
#else
    egress_buffers.push_front(egress_port, std::move(packet));
#endif
}

// used for ingress cloning, resubmit
std::unique_ptr<Packet>
SimpleSwitch::copy_ingress_pkt(
    const std::unique_ptr<Packet> &packet,
    PktInstanceType copy_type, p4object_id_t field_list_id) {
  std::unique_ptr<Packet> packet_copy = packet->clone_no_phv_ptr();
  PHV *phv_copy = packet_copy->get_phv();
  phv_copy->reset_metadata();
  FieldList *field_list = this->get_field_list(field_list_id);
  field_list->copy_fields_between_phvs(phv_copy, packet->get_phv());
  phv_copy->get_field("standard_metadata.instance_type").set(copy_type);
  return packet_copy;
}

void
SimpleSwitch::check_queueing_metadata() {
  bool enq_timestamp_e = field_exists("queueing_metadata", "enq_timestamp");
  bool enq_qdepth_e = field_exists("queueing_metadata", "enq_qdepth");
  bool deq_timedelta_e = field_exists("queueing_metadata", "deq_timedelta");
  bool deq_qdepth_e = field_exists("queueing_metadata", "deq_qdepth");
  if (enq_timestamp_e || enq_qdepth_e || deq_timedelta_e || deq_qdepth_e) {
    if (enq_timestamp_e && enq_qdepth_e && deq_timedelta_e && deq_qdepth_e)
      with_queueing_metadata = true;
    else
      bm::Logger::get()->warn(
          "Your JSON input defines some but not all queueing metadata fields");
  }
}

void
SimpleSwitch::ingress_thread() {
  PHV *phv;

  while (1) {
    std::unique_ptr<Packet> packet;
    input_buffer.pop_back(&packet);

    // TODO(antonin): only update these if swapping actually happened?
    Parser *parser = this->get_parser("parser");
    Pipeline *ingress_mau = this->get_pipeline("ingress");

    phv = packet->get_phv();

    int ingress_port = packet->get_ingress_port();
    (void) ingress_port;
    BMLOG_DEBUG_PKT(*packet, "Processing packet received on port {}",
                    ingress_port);

    /* This looks like it comes out of the blue. However this is needed for
       ingress cloning. The parser updates the buffer state (pops the parsed
       headers) to make the deparser's job easier (the same buffer is
       re-used). But for ingress cloning, the original packet is needed. This
       kind of looks hacky though. Maybe a better solution would be to have the
       parser leave the buffer unchanged, and move the pop logic to the
       deparser. TODO? */
    const Packet::buffer_state_t packet_in_state = packet->save_buffer_state();
    parser->parse(packet.get());

    ingress_mau->apply(packet.get());

    packet->reset_exit();

    Field &f_egress_spec = phv->get_field("standard_metadata.egress_spec");
    int egress_spec = f_egress_spec.get_int();

    Field &f_clone_spec = phv->get_field("standard_metadata.clone_spec");
    unsigned int clone_spec = f_clone_spec.get_uint();

    int learn_id = 0;
    unsigned int mgid = 0u;

    if (phv->has_field("intrinsic_metadata.lf_field_list")) {
      Field &f_learn_id = phv->get_field("intrinsic_metadata.lf_field_list");
      learn_id = f_learn_id.get_int();
    }

    // detect mcast support, if this is true we assume that other fields needed
    // for mcast are also defined
    if (phv->has_field("intrinsic_metadata.mcast_grp")) {
      Field &f_mgid = phv->get_field("intrinsic_metadata.mcast_grp");
      mgid = f_mgid.get_uint();
    }

    int egress_port;

    // INGRESS CLONING
    if (clone_spec) {
      BMLOG_DEBUG_PKT(*packet, "Cloning packet at ingress");
      egress_port = get_mirroring_mapping(clone_spec & 0xFFFF);
      f_clone_spec.set(0);
      if (egress_port >= 0) {
        const Packet::buffer_state_t packet_out_state =
            packet->save_buffer_state();
        packet->restore_buffer_state(packet_in_state);
        p4object_id_t field_list_id = clone_spec >> 16;
        auto packet_copy = copy_ingress_pkt(
            packet, PKT_INSTANCE_TYPE_INGRESS_CLONE, field_list_id);
        // we need to parse again
        // the alternative would be to pay the (huge) price of PHV copy for
        // every ingress packet
        parser->parse(packet_copy.get());
        enqueue(egress_port, std::move(packet_copy));
        packet->restore_buffer_state(packet_out_state);
      }
    }

    // LEARNING
    if (learn_id > 0) {
      get_learn_engine()->learn(learn_id, *packet.get());
    }

    // RESUBMIT
    if (phv->has_field("intrinsic_metadata.resubmit_flag")) {
      Field &f_resubmit = phv->get_field("intrinsic_metadata.resubmit_flag");
      if (f_resubmit.get_int()) {
        BMLOG_DEBUG_PKT(*packet, "Resubmitting packet");
        // get the packet ready for being parsed again at the beginning of
        // ingress
        packet->restore_buffer_state(packet_in_state);
        p4object_id_t field_list_id = f_resubmit.get_int();
        f_resubmit.set(0);
        // TODO(antonin): a copy is not needed here, but I don't yet have an
        // optimized way of doing this
        auto packet_copy = copy_ingress_pkt(
            packet, PKT_INSTANCE_TYPE_RESUBMIT, field_list_id);
        input_buffer.push_front(std::move(packet_copy));
        continue;
      }
    }

    Field &f_instance_type = phv->get_field("standard_metadata.instance_type");

    // MULTICAST
    int instance_type = f_instance_type.get_int();
    if (mgid != 0) {
      BMLOG_DEBUG_PKT(*packet, "Multicast requested for packet");
      Field &f_rid = phv->get_field("intrinsic_metadata.egress_rid");
      const auto pre_out = pre->replicate({mgid});
      auto packet_size = packet->get_register(PACKET_LENGTH_REG_IDX);
      for (const auto &out : pre_out) {
        egress_port = out.egress_port;
        // if (ingress_port == egress_port) continue; // pruning
        BMLOG_DEBUG_PKT(*packet, "Replicating packet on port {}", egress_port);
        f_rid.set(out.rid);
        f_instance_type.set(PKT_INSTANCE_TYPE_REPLICATION);
        std::unique_ptr<Packet> packet_copy = packet->clone_with_phv_ptr();
        packet_copy->set_register(PACKET_LENGTH_REG_IDX, packet_size);
        enqueue(egress_port, std::move(packet_copy));
      }
      f_instance_type.set(instance_type);

      // when doing multicast, we discard the original packet
      continue;
    }

    egress_port = egress_spec;
    BMLOG_DEBUG_PKT(*packet, "Egress port is {}", egress_port);

    if (egress_port == 511) {  // drop packet
      BMLOG_DEBUG_PKT(*packet, "Dropping packet at the end of ingress");
      continue;
    }

    enqueue(egress_port, std::move(packet));
  }
}

void
SimpleSwitch::egress_thread(size_t worker_id) {
  PHV *phv;

  while (1) {
    std::unique_ptr<Packet> packet;
    size_t port;
    egress_buffers.pop_back(worker_id, &port, &packet);

    Deparser *deparser = this->get_deparser("deparser");
    Pipeline *egress_mau = this->get_pipeline("egress");

    phv = packet->get_phv();

    if (with_queueing_metadata) {
      auto enq_timestamp =
          phv->get_field("queueing_metadata.enq_timestamp").get<ts_res::rep>();
      phv->get_field("queueing_metadata.deq_timedelta").set(
          get_ts().count() - enq_timestamp);
      phv->get_field("queueing_metadata.deq_qdepth").set(
          egress_buffers.size(port));
    }

    phv->get_field("standard_metadata.egress_port").set(port);

    Field &f_egress_spec = phv->get_field("standard_metadata.egress_spec");
    f_egress_spec.set(0);

    phv->get_field("standard_metadata.packet_length").set(
        packet->get_register(PACKET_LENGTH_REG_IDX));

    egress_mau->apply(packet.get());

    Field &f_clone_spec = phv->get_field("standard_metadata.clone_spec");
    unsigned int clone_spec = f_clone_spec.get_uint();

    // EGRESS CLONING
    if (clone_spec) {
      BMLOG_DEBUG_PKT(*packet, "Cloning packet at egress");
      int egress_port = get_mirroring_mapping(clone_spec & 0xFFFF);
      if (egress_port >= 0) {
        f_clone_spec.set(0);
        p4object_id_t field_list_id = clone_spec >> 16;
        std::unique_ptr<Packet> packet_copy =
            packet->clone_with_phv_reset_metadata_ptr();
        PHV *phv_copy = packet_copy->get_phv();
        FieldList *field_list = this->get_field_list(field_list_id);
        field_list->copy_fields_between_phvs(phv_copy, phv);
        phv_copy->get_field("standard_metadata.instance_type")
            .set(PKT_INSTANCE_TYPE_EGRESS_CLONE);
        enqueue(egress_port, std::move(packet_copy));
      }
    }

    // TODO(antonin): should not be done like this in egress pipeline
    int egress_spec = f_egress_spec.get_int();
    if (egress_spec == 511) {  // drop packet
      BMLOG_DEBUG_PKT(*packet, "Dropping packet at the end of egress");
      continue;
    }

    deparser->deparse(packet.get());

    // RECIRCULATE
    if (phv->has_field("intrinsic_metadata.recirculate_flag")) {
      Field &f_recirc = phv->get_field("intrinsic_metadata.recirculate_flag");
      if (f_recirc.get_int()) {
        BMLOG_DEBUG_PKT(*packet, "Recirculating packet");
        p4object_id_t field_list_id = f_recirc.get_int();
        f_recirc.set(0);
        FieldList *field_list = this->get_field_list(field_list_id);
        // TODO(antonin): just like for resubmit, there is no need for a copy
        // here, but it is more convenient for this first prototype
        std::unique_ptr<Packet> packet_copy = packet->clone_no_phv_ptr();
        PHV *phv_copy = packet_copy->get_phv();
        phv_copy->reset_metadata();
        field_list->copy_fields_between_phvs(phv_copy, phv);
        phv_copy->get_field("standard_metadata.instance_type")
            .set(PKT_INSTANCE_TYPE_RECIRC);
        size_t packet_size = packet_copy->get_data_size();
        packet_copy->set_register(PACKET_LENGTH_REG_IDX, packet_size);
        phv_copy->get_field("standard_metadata.packet_length").set(packet_size);
        input_buffer.push_front(std::move(packet_copy));
        continue;
      }
    }

    output_buffer.push_front(std::move(packet));
  }
}
