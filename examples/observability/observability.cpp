/**********************************************
* Copyright 2026 Nokia
* Licensed under the BSD 3-Clause Clear License
* SPDX-License-Identifier: BSD-3-Clause-Clear
**********************************************/

#include "observability.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <fstream>
#include <mutex>
#include <set>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <vector>

#include <common/direction.h>
#include <pdcp_layer/pkt_fate.h>
#include <ue/ue.h>
#include <utils/control/param_registry.h>
#include <utils/monitoring/monitoring_manager.h>

namespace observability
{

namespace
{

//--------------------------------------------------------------------------------------------------
// The table. Fate observers of different UEs run concurrently, one thread per UE, so every cell is
// a set of relaxed atomics: the counters are independent and nothing orders one against another.
//--------------------------------------------------------------------------------------------------
struct cell
{
    std::atomic<std::uint64_t> packets;
    std::atomic<std::uint64_t> delivered_bits;
    std::atomic<std::uint64_t> dropped_bits;
    std::atomic<std::uint64_t> ce_packets;
    std::atomic<std::uint64_t> delay_ns;
};

// Statics are zero-initialised, which is the initial state of every counter.
cell table[2][max_ues][class_count];
std::atomic<std::uint64_t> score_table[2][max_ues];
std::atomic<std::uint64_t> untracked{0};
std::atomic<std::uint64_t> outside{0};

constexpr auto relaxed = std::memory_order_relaxed;

bool valid_slot(int tx_dir, int ue_id)
{
    return (tx_dir == TX_DL || tx_dir == TX_UL) && ue_id >= 0 && ue_id < max_ues;
}

//--------------------------------------------------------------------------------------------------
// Packets between ingest and fate. The ingest observer and the fate observer both run on the phase
// thread of the UE that owns the queue, but queues of different UEs share the table, so it has a
// lock. The critical sections are a hash lookup. A design that has to avoid even that keeps one
// table per queue, which is only ever touched by the thread that owns it.
//
// In live mode the key is the NFQUEUE packet id, which is unique within a queue.
//--------------------------------------------------------------------------------------------------
struct pending_table
{
    std::mutex lock;
    std::unordered_map<std::uint64_t, std::uint8_t> dscp;
};

pending_table &pending()
{
    static pending_table table_of_pending;
    return table_of_pending;
}

std::uint64_t pending_key(int queue_num, std::uint32_t uid)
{
    return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(queue_num)) << 32) | uid;
}

void on_ingest(std::uint32_t uid, int, int queue_num, const ext_blob &blob)
{
    if (blob.kind != blob_kind)
        return;
    pending_table &p = pending();
    std::lock_guard<std::mutex> guard(p.lock);
    if (p.dscp.size() >= max_pending_packets)
    {
        untracked.fetch_add(1, relaxed);
        return;
    }
    p.dscp[pending_key(queue_num, uid)] = blob.data[0] & 0x3f;
}

int take_class(int queue_num, std::uint32_t uid)
{
    pending_table &p = pending();
    std::lock_guard<std::mutex> guard(p.lock);
    const auto found = p.dscp.find(pending_key(queue_num, uid));
    if (found == p.dscp.end())
        return unclassified;
    const int cls = found->second;
    p.dscp.erase(found);
    return cls;
}

void on_fate(const pkt_fate_event &event)
{
    // A packet that was ingested always leaves the pending table, whatever its fate and even when
    // the event cannot be accounted for.
    const int cls = take_class(event.queue_num, event.uid);
    if (!valid_slot(event.tx_dir, event.ue_id))
    {
        outside.fetch_add(1, relaxed);
        return;
    }
    cell &c = table[event.tx_dir][event.ue_id][cls];
    c.packets.fetch_add(1, relaxed);
    if (event.fate == bit_fate::delivered)
        c.delivered_bits.fetch_add(event.bits, relaxed);
    else
        c.dropped_bits.fetch_add(event.bits, relaxed);
    if (event.ce)
        c.ce_packets.fetch_add(1, relaxed);
    c.delay_ns.fetch_add(
        static_cast<std::uint64_t>(std::llround(std::max(event.delay_s, 0.0) * 1e9)), relaxed);
}

//--------------------------------------------------------------------------------------------------
// Report selection of each UE. Both the parameters and the controller run on the simulator thread,
// at the quiescent point of a TTI, so this needs no synchronisation.
//--------------------------------------------------------------------------------------------------
struct selection
{
    bool report = true;
    bool all = true;
    std::set<int> classes;   // DSCP values, and unclassified for "none"
};

std::vector<selection> &selections()
{
    static std::vector<selection> s(max_ues);
    return s;
}

std::string trim(const std::string &s)
{
    const std::size_t first = s.find_first_not_of(" \t");
    if (first == std::string::npos)
        return std::string();
    const std::size_t last = s.find_last_not_of(" \t");
    return s.substr(first, last - first + 1);
}

bool parse_class(const std::string &token, int &out)
{
    if (token == "none")
    {
        out = unclassified;
        return true;
    }
    if (token.empty() || token.size() > 2)
        return false;
    int value = 0;
    for (char ch : token)
    {
        if (ch < '0' || ch > '9')
            return false;
        value = value * 10 + (ch - '0');
    }
    if (value >= dscp_classes)
        return false;
    out = value;
    return true;
}

bool known_ue(int ue_id, std::string &reason)
{
    if (ue_id >= 0 && ue_id < max_ues)
        return true;
    reason = "this example accounts for UE ids below " + std::to_string(max_ues);
    return false;
}

std::string direction_tag(int tx_dir)
{
    return tx_dir == TX_DL ? "dl" : "ul";
}

std::size_t flat_index(int tx_dir, std::size_t ue, std::size_t ues, int cls)
{
    return (static_cast<std::size_t>(tx_dir) * ues + ue) * class_count + static_cast<std::size_t>(cls);
}

} // namespace

//--------------------------------------------------------------------------------------------------
// Annotator
//--------------------------------------------------------------------------------------------------
bool annotate_ipv4(const nfq_packet_metadata &meta, ext_blob &blob)
{
    const std::vector<std::uint8_t> &p = meta.payload;
    if (p.size() < 20 || (p[0] >> 4) != 4)
        return false;
    const std::size_t header_bytes = static_cast<std::size_t>(p[0] & 0x0f) * 4;
    if (header_bytes < 20 || header_bytes > p.size())
        return false;
    blob.data[0] = static_cast<std::uint8_t>(p[1] >> 2);
    blob.data[1] = p[9];
    blob.kind = blob_kind;
    return true;
}

//--------------------------------------------------------------------------------------------------
// Counters
//--------------------------------------------------------------------------------------------------
class_counters read_counters(int tx_dir, int ue_id, int cls)
{
    class_counters out;
    if (!valid_slot(tx_dir, ue_id) || cls < 0 || cls >= class_count)
        return out;
    const cell &c = table[tx_dir][ue_id][cls];
    out.packets = c.packets.load(relaxed);
    out.delivered_bits = c.delivered_bits.load(relaxed);
    out.dropped_bits = c.dropped_bits.load(relaxed);
    out.ce_packets = c.ce_packets.load(relaxed);
    out.delay_ns = c.delay_ns.load(relaxed);
    return out;
}

void count_score(int tx_dir, int ue_id) noexcept
{
    if (valid_slot(tx_dir, ue_id))
        score_table[tx_dir][ue_id].fetch_add(1, relaxed);
    else
        outside.fetch_add(1, relaxed);
}

std::uint64_t score_calls(int tx_dir, int ue_id)
{
    return valid_slot(tx_dir, ue_id) ? score_table[tx_dir][ue_id].load(relaxed) : 0;
}

std::size_t pending_packets()
{
    pending_table &p = pending();
    std::lock_guard<std::mutex> guard(p.lock);
    return p.dscp.size();
}

std::uint64_t untracked_packets()
{
    return untracked.load(relaxed);
}

std::uint64_t out_of_range()
{
    return outside.load(relaxed);
}

void reset_for_tests()
{
    for (int dir = 0; dir < 2; dir++)
    {
        for (int ue = 0; ue < max_ues; ue++)
        {
            score_table[dir][ue].store(0, relaxed);
            for (int cls = 0; cls < class_count; cls++)
            {
                cell &c = table[dir][ue][cls];
                c.packets.store(0, relaxed);
                c.delivered_bits.store(0, relaxed);
                c.dropped_bits.store(0, relaxed);
                c.ce_packets.store(0, relaxed);
                c.delay_ns.store(0, relaxed);
            }
        }
    }
    untracked.store(0, relaxed);
    outside.store(0, relaxed);
    pending_table &p = pending();
    std::lock_guard<std::mutex> guard(p.lock);
    p.dscp.clear();
    selections().assign(max_ues, selection());
}

//--------------------------------------------------------------------------------------------------
// Runtime parameters
//--------------------------------------------------------------------------------------------------
bool set_report_enabled(int ue_id, bool enabled, std::string &reason)
{
    if (!known_ue(ue_id, reason))
        return false;
    selections()[static_cast<std::size_t>(ue_id)].report = enabled;
    return true;
}

bool report_enabled(int ue_id)
{
    return ue_id >= 0 && ue_id < max_ues && selections()[static_cast<std::size_t>(ue_id)].report;
}

bool set_filter(int ue_id, const std::string &text, std::string &reason)
{
    if (!known_ue(ue_id, reason))
        return false;
    selection next = selection();
    next.report = selections()[static_cast<std::size_t>(ue_id)].report;
    if (!trim(text).empty())
    {
        next.all = false;
        std::size_t begin = 0;
        for (;;)
        {
            const std::size_t comma = text.find(',', begin);
            const std::string token =
                trim(text.substr(begin, comma == std::string::npos ? std::string::npos : comma - begin));
            int cls = 0;
            if (!parse_class(token, cls))
            {
                reason = "'" + token + "' is not a DSCP value (0 to 63) or the word none";
                return false;
            }
            next.classes.insert(cls);
            if (comma == std::string::npos)
                break;
            begin = comma + 1;
        }
    }
    selections()[static_cast<std::size_t>(ue_id)] = next;
    return true;
}

std::string filter_text(int ue_id)
{
    if (ue_id < 0 || ue_id >= max_ues)
        return std::string();
    const selection &s = selections()[static_cast<std::size_t>(ue_id)];
    std::string out;
    if (s.all)
        return out;
    // The set is ordered, so "none" (the largest index) comes last.
    for (int cls : s.classes)
    {
        if (!out.empty())
            out += ",";
        out += cls == unclassified ? std::string("none") : std::to_string(cls);
    }
    return out;
}

bool class_selected(int ue_id, int cls)
{
    if (ue_id < 0 || ue_id >= max_ues)
        return false;
    const selection &s = selections()[static_cast<std::size_t>(ue_id)];
    return s.all || s.classes.count(cls) > 0;
}

//--------------------------------------------------------------------------------------------------
// Controller
//--------------------------------------------------------------------------------------------------
dscp_reporter::dscp_reporter(std::uint64_t period_tti, std::size_t n_ues, point_sink sink)
    : period_tti_(period_tti),
      sink_(std::move(sink)),
      tracked_ues_(std::min<std::size_t>(n_ues, max_ues))
{
    if (period_tti_ == 0)
        throw std::invalid_argument("period_tti must be at least 1");
    if (!sink_)
    {
        sink_ = [](const metric_point &point) { monitoring_manager::instance().publish(point); };
    }
    // Start from the totals of this moment, so that a report covers the run of this controller.
    last_counters_.reset(new class_counters[2 * tracked_ues_ * class_count]);
    last_calls_.reset(new std::uint64_t[2 * tracked_ues_]);
    for (int dir = 0; dir < 2; dir++)
    {
        for (std::size_t ue = 0; ue < tracked_ues_; ue++)
        {
            last_calls_[static_cast<std::size_t>(dir) * tracked_ues_ + ue] =
                score_calls(dir, static_cast<int>(ue));
            for (int cls = 0; cls < class_count; cls++)
                last_counters_[flat_index(dir, ue, tracked_ues_, cls)] =
                    read_counters(dir, static_cast<int>(ue), cls);
        }
    }
}

void dscp_reporter::on_tick(const tick_context &ctx)
{
    if (ctx.tti == 0 || ctx.tti % period_tti_ != 0)
        return;
    report(ctx.ues.size());
}

void dscp_reporter::report(std::size_t n_ues)
{
    const std::size_t ues = std::min(n_ues, tracked_ues_);
    for (int dir = 0; dir < 2; dir++)
    {
        for (std::size_t ue = 0; ue < ues; ue++)
        {
            const int ue_id = static_cast<int>(ue);
            const bool reported = report_enabled(ue_id);
            const std::string ue_tag = std::to_string(ue_id);

            for (int cls = 0; cls < class_count; cls++)
            {
                class_counters &before = last_counters_[flat_index(dir, ue, tracked_ues_, cls)];
                const class_counters now = read_counters(dir, ue_id, cls);
                const std::uint64_t packets = now.packets - before.packets;
                const class_counters delta{
                    packets,
                    now.delivered_bits - before.delivered_bits,
                    now.dropped_bits - before.dropped_bits,
                    now.ce_packets - before.ce_packets,
                    now.delay_ns - before.delay_ns};
                before = now;
                if (!reported || packets == 0 || !class_selected(ue_id, cls))
                    continue;

                metric_point point;
                point.measurement = "ex_dscp";
                point.tags["ue"] = ue_tag;
                point.tags["dir"] = direction_tag(dir);
                point.tags["dscp"] = cls == unclassified ? std::string("none") : std::to_string(cls);
                point.fields["packets"] = {static_cast<double>(delta.packets), field_aggregation::sum};
                point.fields["delivered_bits"] =
                    {static_cast<double>(delta.delivered_bits), field_aggregation::sum};
                point.fields["dropped_bits"] =
                    {static_cast<double>(delta.dropped_bits), field_aggregation::sum};
                point.fields["ce_packets"] =
                    {static_cast<double>(delta.ce_packets), field_aggregation::sum};
                point.fields["delay_ms"] =
                    {static_cast<double>(delta.delay_ns) * 1e-6 / static_cast<double>(delta.packets),
                     field_aggregation::mean};
                sink_(point);
            }

            std::uint64_t &seen = last_calls_[static_cast<std::size_t>(dir) * tracked_ues_ + ue];
            const std::uint64_t calls = score_calls(dir, ue_id);
            const std::uint64_t scored = calls - seen;
            seen = calls;
            if (reported && scored > 0)
            {
                metric_point point;
                point.measurement = "ex_sched";
                point.tags["ue"] = ue_tag;
                point.tags["dir"] = direction_tag(dir);
                point.fields["score_calls"] = {static_cast<double>(scored), field_aggregation::sum};
                sink_(point);
            }
        }
    }
}

namespace
{

bool parse_unsigned(const std::string &text, std::uint64_t &out)
{
    if (text.empty() || text.size() > 12)
        return false;
    std::uint64_t value = 0;
    for (char ch : text)
    {
        if (ch < '0' || ch > '9')
            return false;
        value = value * 10 + static_cast<std::uint64_t>(ch - '0');
    }
    out = value;
    return true;
}

} // namespace

std::unique_ptr<controller> make_reporter(
    const std::string &config_file, std::size_t n_ues, point_sink sink)
{
    std::ifstream in(config_file);
    if (!in.is_open())
        return nullptr;

    bool present = false;
    bool enabled = true;
    std::uint64_t period_tti = 100;
    bool in_section = false;
    std::string line;
    while (std::getline(in, line))
    {
        const std::string t = trim(line);
        if (t.empty() || t[0] == '#' || t[0] == ';')
            continue;
        if (t[0] == '[')
        {
            in_section = (t == "[Observability]");
            present = present || in_section;
            continue;
        }
        if (!in_section)
            continue;
        const std::size_t sep = t.find_first_of(":=");
        if (sep == std::string::npos)
            throw std::invalid_argument("[Observability] line without a key: '" + t + "'");
        const std::string key = trim(t.substr(0, sep));
        const std::string value = trim(t.substr(sep + 1));
        if (key == "enabled")
        {
            if (value != "true" && value != "false")
                throw std::invalid_argument("[Observability] enabled must be true or false");
            enabled = (value == "true");
        }
        else if (key == "period_tti")
        {
            if (!parse_unsigned(value, period_tti) || period_tti == 0)
                throw std::invalid_argument("[Observability] period_tti must be a positive integer");
        }
        else
        {
            throw std::invalid_argument("[Observability] unknown key '" + key + "'");
        }
    }
    if (!present || !enabled)
        return nullptr;
    return std::unique_ptr<controller>(new dscp_reporter(period_tti, n_ues, std::move(sink)));
}

//--------------------------------------------------------------------------------------------------
// Registration. Everything below runs before main(), when this object file is linked in.
//--------------------------------------------------------------------------------------------------
namespace
{

bool register_all()
{
    register_annotator(&annotate_ipv4);
    register_ingest_observer(&on_ingest);
    register_fate_observer(&on_fate);

    register_controller(
        "observability",
        [](const std::string &config_file, const cell_info &, const std::vector<ue> &ues)
        { return make_reporter(config_file, ues.size(), point_sink()); });

    param_entry report;
    report.name = "example.dscp_report";
    report.type = param_type::boolean;
    report.description =
        "Whether the observability example reports the DSCP counters of this UE. Default true.";
    report.apply = [](ue &u, const param_value &v, std::string &reason)
    {
        const bool *on = std::get_if<bool>(&v);
        return on != nullptr && set_report_enabled(u.get_id(), *on, reason);
    };
    report.read = [](ue &u) { return report_enabled(u.get_id()) ? 1.0 : 0.0; };
    param_registry::add(std::move(report));

    param_entry filter;
    filter.name = "example.dscp_filter";
    filter.type = param_type::text;
    filter.max_length = 256;
    filter.description =
        "DSCP classes that the observability example reports for this UE: a comma-separated list "
        "of values from 0 to 63 and the word none, for example 46,10,none. Empty reports every "
        "class.";
    filter.apply = [](ue &u, const param_value &v, std::string &reason)
    {
        const std::string *text = std::get_if<std::string>(&v);
        return text != nullptr && set_filter(u.get_id(), *text, reason);
    };
    filter.read_text = [](ue &u) { return filter_text(u.get_id()); };
    param_registry::add(std::move(filter));
    return true;
}

[[maybe_unused]] const bool registered = register_all();

} // namespace

} // namespace observability
