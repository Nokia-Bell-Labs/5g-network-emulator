/**********************************************
* Copyright 2026 Nokia
* Licensed under the BSD 3-Clause Clear License
* SPDX-License-Identifier: BSD-3-Clause-Clear
**********************************************/

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include <mac_layer/mac_definitions.h>
#include <netfilter/capture_annotation.h>
#include <simulator/controller_registry.h>
#include <utils/monitoring/aggregator.h>

//--------------------------------------------------------------------------------------------------
// Observability example: per-UE, per-DSCP-class accounting built on the extension points.
//
//  - An annotator reads the DSCP and the protocol of every captured IPv4 packet.
//  - An ingest observer remembers the DSCP of each packet until its fate is known.
//  - A fate observer adds the packet to the counters of its UE, direction and DSCP class.
//  - A controller publishes the change of those counters every N TTIs as telemetry.
//  - A metric, "pf_counted", scores exactly like proportional fair and counts how often each UE
//    is scored.
//  - Two runtime parameters choose what is reported for a UE.
//
// Nothing here changes a decision of the emulator. It only reads and counts.
//
// The module is compiled on demand (make example-observability); see README.md. Its registrations
// are file-scope objects, so linking the object files in is all it takes to activate them.
//--------------------------------------------------------------------------------------------------

namespace observability
{

// Largest UE id the example accounts for. Events of a higher id are counted in out_of_range().
constexpr int max_ues = 256;

// A class is a DSCP value (0..63) or "no annotation" (index unclassified). Packets that did not
// pass the annotator, such as those of a simulated source, fall in the second one.
constexpr int dscp_classes = 64;
constexpr int unclassified = dscp_classes;
constexpr int class_count = dscp_classes + 1;

// Value of ext_blob::kind for the records written by this example.
constexpr std::uint16_t blob_kind = 0x4453;

// Id of the instrumented metric, the first one that is free for outside use.
constexpr int counted_pf_metric_id = METRIC_EXTERNAL_FIRST;

// Bound of the table of packets between ingest and fate. A packet that finds it full is not
// classified and is counted in untracked_packets().
constexpr std::size_t max_pending_packets = 65536;

//--------------------------------------------------------------------------------------------------
// Annotator (called by the capture; exposed so that a test can call it directly)
//
// Layout of the record: data[0] = DSCP, data[1] = IP protocol, kind = blob_kind. Returns false for
// anything that is not a well-formed IPv4 header. The ECN bits are left alone: the L4S queue
// classifies by them, and this module only reads.
//--------------------------------------------------------------------------------------------------
bool annotate_ipv4(const nfq_packet_metadata &meta, ext_blob &blob);

//--------------------------------------------------------------------------------------------------
// Counters
//--------------------------------------------------------------------------------------------------
struct class_counters
{
    std::uint64_t packets = 0;
    std::uint64_t delivered_bits = 0;
    // Bits of packets that ended in any fate other than delivered.
    std::uint64_t dropped_bits = 0;
    std::uint64_t ce_packets = 0;
    // Sum over packets of the time from arrival to fate, in nanoseconds.
    std::uint64_t delay_ns = 0;
};

// A snapshot of one cell of the table. tx_dir is TX_DL or TX_UL, cls a DSCP value or
// unclassified. Reads are relaxed: the fields of one snapshot are not mutually consistent while
// packets are in flight, which is fine for telemetry.
class_counters read_counters(int tx_dir, int ue_id, int cls);

// Called by the instrumented metric for every score. Safe from any thread, never throws.
void count_score(int tx_dir, int ue_id) noexcept;

// Times the instrumented metric has scored this UE in this direction.
std::uint64_t score_calls(int tx_dir, int ue_id);

// Packets between ingest and fate, packets that could not be tracked, and events that fell
// outside the table (a UE id of max_ues or more, or a handler that was not told its direction).
std::size_t pending_packets();
std::uint64_t untracked_packets();
std::uint64_t out_of_range();

// For tests: zeroes every counter and forgets the pending packets.
void reset_for_tests();

//--------------------------------------------------------------------------------------------------
// Runtime parameters
//
// "example.dscp_report" (boolean) turns the report of a UE on or off, default on, and
// "example.dscp_filter" (text) selects the classes it reports: a comma-separated list of DSCP
// values and the word none, for example "46,10,none". Empty means every class. The functions
// below are what the parameter entries call.
//--------------------------------------------------------------------------------------------------
bool set_report_enabled(int ue_id, bool enabled, std::string &reason);
bool report_enabled(int ue_id);
bool set_filter(int ue_id, const std::string &text, std::string &reason);
std::string filter_text(int ue_id);
bool class_selected(int ue_id, int cls);

//--------------------------------------------------------------------------------------------------
// Controller
//--------------------------------------------------------------------------------------------------
using point_sink = std::function<void(const metric_point &)>;

// Every `period_tti` TTIs, publishes what changed since the previous report:
//   - measurement ex_dscp, tags ue, dir (dl or ul) and dscp (a number or none); fields packets,
//     delivered_bits, dropped_bits and ce_packets (sums) and delay_ms (mean per packet).
//   - measurement ex_sched, tags ue and dir; field score_calls (sum).
// A UE whose report is off is skipped and its baseline moves on, so that turning it on again
// reports only what happened since. The sink defaults to the monitoring manager, which drops the
// point when monitoring is disabled.
struct dscp_reporter : public controller
{
    dscp_reporter(std::uint64_t period_tti, std::size_t n_ues, point_sink sink);

    void on_tick(const tick_context &ctx) override;

    // One report, called by on_tick on the period and directly by tests.
    void report(std::size_t n_ues);

    // State of the reporter. Nothing outside it needs these, and tests use report() and on_tick().
    std::uint64_t period_tti_;
    point_sink sink_;
    // Totals at the previous report, indexed [dir][ue][class] and [dir][ue].
    std::unique_ptr<class_counters[]> last_counters_;
    std::unique_ptr<std::uint64_t[]> last_calls_;
    std::size_t tracked_ues_;
};

// Reads the [Observability] section: period_tti (default 100) and enabled (default true).
// Returns nullptr when the section is absent or disabled; throws std::invalid_argument for a
// malformed value.
std::unique_ptr<controller> make_reporter(
    const std::string &config_file, std::size_t n_ues, point_sink sink);

} // namespace observability
