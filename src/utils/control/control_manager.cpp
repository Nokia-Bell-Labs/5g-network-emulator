/**********************************************
* Copyright 2026 Nokia
* Licensed under the BSD 3-Clause Clear License
* SPDX-License-Identifier: BSD-3-Clause-Clear
**********************************************/

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>

#include <mac_layer/mac_definitions.h>
#include <utils/monitoring/monitoring_manager.h>
#include <ue/ue.h>
#include <ue/ue_handler.h>
#include <nlohmann/json.hpp>
#include <utils/control/control_manager.h>
#include <utils/control/ndjson.h>
#include <utils/control/param_registry.h>
#include <utils/control/transport_file.h>
#include <utils/control/transport_socket.h>
#include <utils/conversions.h>
#include <utils/terminal_logging.h>

control_manager::control_manager() {}

control_manager::~control_manager()
{
    stop();
}

void control_manager::init(const control_config &cfg, std::vector<ue> *ue_list, const cell_info &cell)
{
    ue_list_ = ue_list;
    max_cmds_per_tick_ = cfg.max_cmds_per_tick;
    max_object_events_ = (size_t)std::max(1, cfg.max_object_events);
    cell_ = cell;
    const float period_ms = cell.period_ms;

    if (!cfg.enabled)
    {
        enabled_ = false;
        return;
    }
    if (cfg.transport == "none")
        throw run_failure(run_exit_code::config,
                          "[Control] enabled needs a transport");
    if (cfg.sync_mode != "async" && cfg.sync_mode != "barrier")
        throw run_failure(run_exit_code::config,
                          "unknown control sync_mode: " + cfg.sync_mode);
    if (cfg.on_timeout != "abort" && cfg.on_timeout != "continue")
        throw run_failure(run_exit_code::config,
                          "unknown control on_timeout: " + cfg.on_timeout);
    if (cfg.on_peer_loss != "abort" && cfg.on_peer_loss != "continue")
        throw run_failure(run_exit_code::config,
                          "unknown control on_peer_loss: " + cfg.on_peer_loss);
    if (cfg.credit_timeout_ms <= 0 || cfg.max_cmds_per_tick <= 0
        || cfg.max_object_events <= 0)
        throw run_failure(run_exit_code::config,
                          "control limits and timeouts must be positive");

    if (cfg.transport == "file")
    {
        if (cfg.timeline_file == "none" || cfg.timeline_file.empty())
        {
            LOG_ERROR_I("control_manager::init") << " transport: file needs a timeline_file" << END();
            throw run_failure(run_exit_code::config,
                              "transport file needs timeline_file");
        }
        std::ifstream timeline(cfg.timeline_file);
        if (!timeline.is_open())
            throw run_failure(run_exit_code::no_input,
                              "cannot open control timeline: " + cfg.timeline_file);
        std::unique_ptr<transport_file> t(new transport_file(cfg.timeline_file));
        if (!t->ok())
        {
            throw run_failure(run_exit_code::config,
                              "invalid control timeline: " + cfg.timeline_file);
        }
        transport_.reset(t.release());
    }
    else if (cfg.transport == "unix" || cfg.transport == "tcp")
    {
        std::unique_ptr<transport_socket> t(new transport_socket(cfg));
        if (!t->ok())
        {
            throw run_failure(run_exit_code::io_error,
                              "cannot create control socket");
        }
        transport_.reset(t.release());
    }
    else
    {
        LOG_ERROR_I("control_manager::init") << " unknown transport: " << cfg.transport << END();
        throw run_failure(run_exit_code::config,
                          "unknown control transport: " + cfg.transport);
    }

    // Index by UE id. ue_handler::init() has already run, so both the vector and the
    // addresses inside it are stable for the rest of the run.
    ue_index_.clear();
    ue_name_index_.clear();
    for (size_t i = 0; i < ue_list_->size(); i++)
    {
        ue &u = (*ue_list_)[i];
        const int id = u.get_id();
        if ((int)ue_index_.size() <= id) ue_index_.resize(id + 1, nullptr);
        ue_index_[id] = &u;
        if (!ue_name_index_.emplace(u.get_control_id(), &u).second)
            throw run_failure(run_exit_code::config,
                              "duplicate textual ue_id: " + u.get_control_id());
    }

    transport_->set_grant_sink([this](const command &c, ack &a) { apply_grant(c, a); });

    // Barrier in real time would mean blocking the wall clock, which defeats the mode.
    mode_ = (cfg.sync_mode == "barrier") ? mode_t::barrier : mode_t::async;
    if (mode_ == mode_t::barrier && period_ms > 0)
    {
        mode_ = mode_t::async;
        LOG_WARNING_I("control_manager::init")
            << " sync_mode: barrier is not available with period > 0; degrading to async" << END();
    }
    // A file has no peer to grant credit, so a barrier over it would never advance.
    if (mode_ == mode_t::barrier && cfg.transport == "file")
    {
        mode_ = mode_t::async;
        LOG_WARNING_I("control_manager::init")
            << " sync_mode: barrier needs a socket transport; degrading to async" << END();
    }

    on_timeout_ = (cfg.on_timeout == "abort") ? on_timeout_t::abort : on_timeout_t::cont;
    on_peer_loss_ = (cfg.on_peer_loss == "continue") ? on_peer_loss_t::cont : on_peer_loss_t::abort;
    timeout_ = std::chrono::milliseconds(cfg.credit_timeout_ms);

    if (cfg.journal_file != "none" && !cfg.journal_file.empty())
    {
        journal_.open(cfg.journal_file, std::ios::out | std::ios::trunc);
        if (!journal_.is_open())
            throw run_failure(run_exit_code::io_error,
                              "cannot open control journal: " + cfg.journal_file);
    }

    transport_open_ = true;
    enabled_ = true;

    LOG_INFO_I("control_manager::init")
        << "Runtime control enabled"
        << " transport=" << cfg.transport
        << " sync_mode=" << (mode_ == mode_t::barrier ? "barrier" : "async")
        << " on_peer_loss=" << (on_peer_loss_ == on_peer_loss_t::abort ? "abort" : "continue")
        << " ues=" << ue_list_->size()
        << " realtime=" << (period_ms > 0 ? "yes" : "no")
        << END();
}

void control_manager::interrupt()
{
    stopping_.store(true);
    cv_.notify_all();
    if (transport_) transport_->stop();
}

void control_manager::stop()
{
    interrupt();
    transport_open_ = false;
    if (journal_.is_open()) journal_.close();
}

// Absolute and monotonic: until_tti is the last TTI the emulator may run. Credit never
// moves backwards, so a grant into the past is a no-op answered with ok and the credit in
// force, which is exactly what a client retrying after a timeout needs.
void control_manager::apply_grant(const command &c, ack &a)
{
    {
        std::lock_guard<std::mutex> lk(mtx_);
        if (c.connection_generation != credit_generation_)
        {
            credit_generation_ = c.connection_generation;
            credit_until_tti_ = -1;
        }
        if (c.until_tti > credit_until_tti_) credit_until_tti_ = c.until_tti;
        a.credit_until_tti = credit_until_tti_;
    }
    a.ok = true;
    cv_.notify_all();
}

void control_manager::wait_for_credit(std::int64_t tti)
{
    if (mode_ != mode_t::barrier) return;

    const std::chrono::steady_clock::time_point wait_start = std::chrono::steady_clock::now();
    std::unique_lock<std::mutex> lk(mtx_);
    const bool granted = cv_.wait_for(lk, timeout_, [&] {
        return (tti <= credit_until_tti_
                && credit_generation_ == transport_->current_generation())
            || stopping_
            || (transport_->peer_ever_connected() && !transport_->peer_alive());
    });

    const double waited_ms = std::chrono::duration_cast<std::chrono::duration<double, std::milli>>(
        std::chrono::steady_clock::now() - wait_start).count();
    if (waited_ms > 0.0)
    {
        blocked_ttis_++;
        blocked_ms_ += waited_ms;
    }

    if (stopping_) return;

    if (transport_->peer_ever_connected() && !transport_->peer_alive())
    {
        if (on_peer_loss_ == on_peer_loss_t::abort)
        {
            // A run without its controller is not the experiment that was asked for:
            // nothing would drive it and the rest of the output would be a silence
            // recorded as if it were data.
            stopping_ = true;
            stop_reason_ = run_stop_reason::control_peer_lost;
            LOG_ERROR_I("control_manager")
                << " control peer lost at tti " << tti << "; aborting the run" << END();
            return;
        }

        // Carrying on free running, and irreversibly: the run finishes unattended, and
        // does not pretend to be synchronised again if someone reconnects.
        mode_ = mode_t::async;
        LOG_WARNING_I("control_manager")
            << " control peer lost at tti " << tti << "; degrading to async for the rest of the run" << END();
        return;
    }

    if (!granted)
    {
        if (on_timeout_ == on_timeout_t::abort)
        {
            stopping_ = true;
            stop_reason_ = run_stop_reason::credit_timeout;
            LOG_ERROR_I("control_manager")
                << " no credit for tti " << tti << " after " << timeout_.count() << " ms; aborting" << END();
            return;
        }

        // Only reachable if it was asked for, and still worth a line: from here on the
        // run is not the synchronised experiment it was configured to be, and without
        // this that would be invisible in the output.
        LOG_WARNING_I("control_manager")
            << " no credit for tti " << tti << " after " << timeout_.count()
            << " ms; running it anyway, the run is no longer in lockstep" << END();
    }
}

void control_manager::write_journal(const command &c, double sim_t, std::int64_t tti)
{
    if (!journal_.is_open()) return;

    const std::int64_t wall_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    // A journal line is a valid script line: replaying it by at_tti reproduces the
    // session step by step, with no conversion in between.
    journal_ << ndjson::serialize_journal_entry(c, sim_t, tti, wall_ns);
    journal_.flush();
}

void control_manager::tick(double sim_t, std::int64_t tti)
{
    if (!enabled_) return;

    wait_for_credit(tti);

    // Changes produced by the previous TTI are now stable: both worker pools are
    // parked, which is the same reason applying control here needs no atomics.
    collect_object_events(tti);
    if (stopping_) return;
    drain_transport();
    apply_due(sim_t, tti);

    if (ranks_dirty_)
    {
        ue_handler::refresh_enabled_ranks(*ue_list_);
        ranks_dirty_ = false;
    }

    refill_rate_buckets();
    publish_metrics(tti);
}

void control_manager::publish_metrics(std::int64_t tti)
{
    // Only when something happened: a point per TTI would put the aggregator's mutex on
    // the path of every step for no information at all.
    if (applied_in_tick_ == 0 && rejected_in_tick_ == 0 && blocked_ttis_ == 0) return;

    monitoring_manager &monitoring = monitoring_manager::instance();
    if (monitoring.is_enabled() && monitoring.get_config().emit_control)
    {
        metric_point point;
        point.measurement = "control";
        point.ts_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();

        metric_field applied;   applied.value = applied_in_tick_;      applied.aggregation = field_aggregation::sum;
        metric_field rejected;  rejected.value = rejected_in_tick_;    rejected.aggregation = field_aggregation::sum;
        metric_field blocked;   blocked.value = blocked_ttis_;         blocked.aggregation = field_aggregation::sum;
        metric_field blocked_t; blocked_t.value = blocked_ms_;         blocked_t.aggregation = field_aggregation::sum;
        metric_field last_tti;  last_tti.value = (double)last_applied_tti_; last_tti.aggregation = field_aggregation::last;
        metric_field credit;    credit.value = (double)credit_until_tti_;   credit.aggregation = field_aggregation::last;

        point.fields["applied_sum"] = applied;
        point.fields["rejected_sum"] = rejected;
        point.fields["blocked_ttis_sum"] = blocked;
        point.fields["blocked_ms_sum"] = blocked_t;
        point.fields["last_applied_tti_last"] = last_tti;
        point.fields["credit_until_tti_last"] = credit;
        monitoring.publish(point);
    }

    (void)tti;
    applied_in_tick_ = 0;
    rejected_in_tick_ = 0;
    blocked_ttis_ = 0;
    blocked_ms_ = 0.0;
}

void control_manager::drain_transport()
{
    if (!transport_open_) return;

    std::vector<command> batch;
    transport_open_ = transport_->poll(batch);

    for (size_t i = 0; i < batch.size(); i++)
    {
        scheduled s;
        s.at_tti = batch[i].at_tti;
        s.seq = seq_++;
        s.cmd = batch[i];
        sched_.push(s);
    }
}

void control_manager::apply_due(double sim_t, std::int64_t tti)
{
    // The cap bounds how long one TTI may take, which only matters when the run is
    // pacing itself against the wall clock. A barrier run never is -- init degrades
    // barrier to async when period > 0 -- and there the client owns the clock. Deferring
    // commands it is blocked on then manufactures a stall it cannot clear: the
    // acknowledgements it is waiting for would only come from a TTI it has not granted,
    // and it cannot grant that TTI until it stops waiting.
    const bool capped = mode_ != mode_t::barrier;
    int applied = 0;
    while (!stopping_.load() && !sched_.empty() && sched_.top().at_tti <= tti
           && (!capped || applied < max_cmds_per_tick_))
    {
        command c = sched_.top().cmd;
        sched_.pop();
        if (!transport_->command_is_current(c)) continue;
        apply(c, sim_t, tti);
        write_journal(c, sim_t, tti);
        last_applied_tti_ = tti;
        applied++;
    }
}

void control_manager::collect_object_events(std::int64_t tti)
{
    if (!object_events_enabled_ || ue_list_ == nullptr) return;

    std::vector<object_event> pending;
    // unordered_map iteration would make the wire order depend on hash layout. The
    // changes within one TTI are simultaneous at this boundary, so UE, direction and
    // tag order is the deterministic order to expose.
    for (size_t i = 0; i < ue_list_->size(); i++)
    {
        ue &u = (*ue_list_)[i];
        for (int tx_dir : {TX_DL, TX_UL})
        {
            std::unordered_map<std::uint32_t, object_counters> changed =
                u.take_object_events(tx_dir);
            std::vector<std::uint32_t> tags;
            tags.reserve(changed.size());
            for (const auto &entry : changed) tags.push_back(entry.first);
            std::sort(tags.begin(), tags.end());

            for (std::uint32_t tag : tags)
            {
                const object_counters &c = changed[tag];
                object_event e;
                // Collected at the next quiescent point: these counters moved while
                // the preceding TTI was running.
                e.at_tti = tti - 1;
                e.ue_id = u.get_id();
                e.ue_name = u.get_control_id();
                e.tx_dir = tx_dir;
                e.tag = tag;
                e.delivered_bytes = c.delivered_bits / 8.0;
                e.expired_bytes = c.expired_bits / 8.0;
                e.queue_dropped_bytes = c.queue_dropped_bits / 8.0;
                e.radio_dropped_bytes = c.radio_dropped_bits / 8.0;
                e.ce_bytes = c.ce_bits / 8.0;
                pending.push_back(e);
            }
        }
    }

    // Once there is a gap, keep draining the handler-local maps so they remain
    // bounded, but do not pretend the global stream can resume without a snapshot.
    if (object_event_gap_) return;
    if (object_events_.size() + pending.size() > max_object_events_)
    {
        object_event_gap_ = true;
        object_events_.clear();
        if (mode_ == mode_t::barrier)
        {
            stopping_ = true;
            stop_reason_ = run_stop_reason::object_event_backlog;
            LOG_ERROR_I("control_manager")
                << " object event backlog exceeded " << max_object_events_
                << " entries; aborting the lockstep run before feedback is lost" << END();
        }
        else
        {
            LOG_WARNING_I("control_manager")
                << " object event backlog exceeded " << max_object_events_
                << " entries; incremental feedback has a gap and needs resync" << END();
        }
        return;
    }

    for (object_event &e : pending)
    {
        e.seq = ++object_event_seq_;
        object_events_.push_back(e);
    }
}

bool control_manager::resolve_target(const std::string &target, std::vector<ue *> &out, ack &a)
{
    if (target == "ue/*")
    {
        for (size_t i = 0; i < ue_list_->size(); i++) out.push_back(&(*ue_list_)[i]);
        return true;
    }

    if (target.compare(0, 3, "ue/") == 0)
    {
        const std::string key = target.substr(3);
        const auto named = ue_name_index_.find(key);
        if (named != ue_name_index_.end())
        {
            out.push_back(named->second);
            return true;
        }

        const bool numeric = !key.empty() && std::all_of(
            key.begin(), key.end(),
            [](unsigned char c) { return std::isdigit(c) != 0; });
        if (numeric)
        {
            const unsigned long long raw = std::stoull(key);
            if (raw < ue_index_.size() && ue_index_[(size_t)raw] != nullptr)
            {
                out.push_back(ue_index_[(size_t)raw]);
                return true;
            }
        }

        ack_error e; e.key = target; e.reason = "unknown ue";
        a.errors.push_back(e);
        return false;
    }

    ack_error e; e.key = target; e.reason = "unknown target";
    a.errors.push_back(e);
    return false;
}

bool control_manager::validate(const command &c, std::vector<ue *> &targets, ack &a)
{
    if (!resolve_target(c.target, targets, a)) return false;

    // Whole message first, nothing applied yet: a ue/* carrying one bad value must not
    // leave half the UEs updated.
    const param_registry &reg = param_registry::instance();
    for (size_t i = 0; i < c.sets.size(); i++)
    {
        const param_entry *e = reg.find(c.sets[i].first);
        if (e == nullptr)
        {
            ack_error err; err.key = c.sets[i].first; err.reason = "unknown";
            a.errors.push_back(err);
            continue;
        }

        std::string reason;
        if (!reg.check(*e, c.sets[i].second, reason))
        {
            ack_error err; err.key = c.sets[i].first; err.reason = reason;
            a.errors.push_back(err);
        }
    }

    return a.errors.empty();
}

void control_manager::apply(const command &c, double sim_t, std::int64_t tti)
{
    ack a;
    a.id = c.id;
    a.connection_generation = c.connection_generation;
    a.tti = tti;
    a.t = sim_t;

    if (c.op == command_op::ping)
    {
        a.ok = true;
        transport_->reply(a);
        return;
    }

    if (c.op == command_op::describe)
    {
        a.ok = true;
        a.payload = param_registry::instance().describe_json();
        transport_->reply(a);
        return;
    }

    if (c.op == command_op::events)
    {
        a.payload = read_object_events(c, a);
        a.ok = a.errors.empty();
        transport_->reply(a);
        return;
    }

    if (c.op == command_op::get)
    {
        if (c.target == "cell")
        {
            a.payload = read_cell_state();
            a.ok = true;
            transport_->reply(a);
            return;
        }

        std::vector<ue *> targets;
        if (!resolve_target(c.target, targets, a))
        {
            a.ok = false;
            transport_->reply(a);
            return;
        }
        a.payload = read_state(targets);
        a.ok = true;
        transport_->reply(a);
        return;
    }

    std::vector<ue *> targets;
    if (!validate(c, targets, a))
    {
        a.ok = false;
        rejected_in_tick_++;
        transport_->reply(a);
        return;
    }

    if (c.op == command_op::inject || c.op == command_op::forget)
    {
        for (size_t t = 0; t < targets.size(); t++)
        {
            const bool done = c.op == command_op::inject
                ? targets[t]->inject_bits(
                    c.tx_dir,
                    static_cast<std::uint64_t>(c.bytes) * 8,
                    c.tag,
                    c.ecn)
                : targets[t]->forget_object(c.tag);
            if (!done)
            {
                ack_error err;
                err.key = c.op == command_op::inject ? "inject" : "forget";
                err.reason = c.op == command_op::inject
                    ? "this UE has no simulated traffic source"
                    : "unknown tag";
                a.errors.push_back(err);
            }
        }

        a.ok = a.errors.empty();
        if (a.ok) applied_in_tick_++;
        else rejected_in_tick_++;
        transport_->reply(a);
        return;
    }

    const param_registry &reg = param_registry::instance();
    for (size_t t = 0; t < targets.size(); t++)
    {
        ue *u = targets[t];
        for (size_t i = 0; i < c.sets.size(); i++)
        {
            const param_entry *e = reg.find(c.sets[i].first);
            std::string reason;
            if (!e->apply(*u, c.sets[i].second, reason))
            {
                ack_error err; err.key = c.sets[i].first; err.reason = reason;
                a.errors.push_back(err);
                continue;
            }

            if (c.sets[i].first == "enabled") ranks_dirty_ = true;
            if (c.sets[i].first == "dl.rmax_mbps" || c.sets[i].first == "ul.rmax_mbps") any_rate_cap_ = true;
            if (c.sets[i].first == "priority") warn_priority_under_rr();
        }
    }

    a.ok = a.errors.empty();
    if (a.ok) applied_in_tick_++;
    else rejected_in_tick_++;
    transport_->reply(a);
}

void control_manager::warn_priority_under_rr()
{
    if (warned_rr_priority_ || cell_.metric_type != METRIC_RR) return;
    warned_rr_priority_ = true;
    LOG_WARNING_I("control_manager")
        << " priority was set while metric_type is round robin: get_metric derives to the"
        << " round robin rotation before applying priority, so the knob has no effect"
        << END();
}

namespace
{
// Everything a client driving its own injection needs in order to pace: what is still
// queued, what came out, and what was lost and why. Cumulative where it makes sense, so
// two reads can be diffed and a lost read costs nothing.
nlohmann::json direction_state(ue &u, int tx_dir, bool include_objects = true)
{
    pdcp_layer &p = u.pdcp_state(tx_dir);
    const pdcp_queue_status q = p.get_queue_status();

    nlohmann::json j;
    j["injected_bytes_total"] = p.injected_bits_total() / 8.0;
    j["delivered_bytes_total"] = p.delivered_bits_total() / 8.0;
    j["expired_bytes_total"] = p.expired_bits_total() / 8.0;
    // dropped_bytes_total is every non-expiry loss, which is what it has always been and
    // what the co-simulation spec closes an object with. The two below split it into the
    // answer a client actually wants: the AQM fires at 15 ms of queue and is asking the
    // sender to slow down, while an exhausted HARQ means the link itself is bad.
    j["dropped_bytes_total"] = p.dropped_bits_total() / 8.0;
    j["queue_dropped_bytes_total"] = p.queue_dropped_bits_total() / 8.0;
    j["radio_dropped_bytes_total"] = p.radio_dropped_bits_total() / 8.0;
    j["ce_packets_total"] = p.ce_packets_total();
    j["pending_packets"] = q.ip_buffer_size;
    j["pending_bytes"] = p.pending_bits() / 8.0;
    j["admitted_bytes_total"] = q.admitted_bits_total / 8.0;
    j["conservation_residual_bytes"] =
        q.conservation_residual_bits / 8.0;
    j["oldest_age_s"] = q.ip_oldest_age;
    j["latency_s"] = p.get_latency(false);
    // Radio state, for a client that wants to know why the bytes are going slowly:
    // the channel it is getting, and how much of the air went on second attempts.
    j["sinr_db"] = u.get_mean_sinr(tx_dir);
    j["retransmitted_bytes_total"] = p.retransmitted_bits_total() / 8.0;
    j["harq_model"] = q.harq_model;
    j["harq_queue_blocks"] = q.harq_size;
    j["harq_queue_high_water_blocks"] = q.harq_high_water_blocks;
    j["harq_queue_capacity_blocks"] = q.harq_capacity_blocks;
    j["harq_oldest_retry_ordinal"] =
        q.harq_oldest_retry_ordinal;
    j["harq_oldest_age_s"] = q.harq_oldest_age;
    j["last_charged_grant_bytes"] =
        q.last_charged_grant_bits / 8.0;

    // Per object counters, for as long as the client keeps the tag alive. Terminal
    // states only: what is neither delivered nor lost is still in flight, which the
    // client knows because it knows how much it injected.
    const std::unordered_map<std::uint32_t, object_counters> &objects = p.objects();
    if (include_objects && !objects.empty())
    {
        nlohmann::json o = nlohmann::json::object();
        for (std::unordered_map<std::uint32_t, object_counters>::const_iterator it = objects.begin();
             it != objects.end(); ++it)
        {
            nlohmann::json c;
            const std::uint64_t terminal_bits =
                it->second.delivered_bits
                + it->second.expired_bits
                + it->second.dropped_bits();
            const std::int64_t unresolved_bits =
                static_cast<std::int64_t>(it->second.injected_bits)
                - static_cast<std::int64_t>(terminal_bits);
            c["injected_bytes"] = it->second.injected_bits / 8.0;
            c["delivered_bytes"] = it->second.delivered_bits / 8.0;
            c["dropped_bytes"] = it->second.dropped_bits() / 8.0;
            c["expired_bytes"] = it->second.expired_bits / 8.0;
            c["queue_dropped_bytes"] = it->second.queue_dropped_bits / 8.0;
            c["radio_dropped_bytes"] = it->second.radio_dropped_bits / 8.0;
            c["ce_bytes"] = it->second.ce_bits / 8.0;
            c["unresolved_bytes"] = unresolved_bits / 8.0;
            o[std::to_string(it->first)] = c;
        }
        j["objects"] = o;
    }
    return j;
}
}

std::string control_manager::read_cell_state() const
{
    nlohmann::json j;
    j["scenario_type"] = cell_.scenario_type;
    j["frequency_hz"] = cell_.frequency_hz;
    j["bandwidth_hz"] = cell_.bandwidth_hz;
    j["numerology"] = cell_.numerology;
    j["n_freq_rbg"] = cell_.n_freq_rbg;
    j["metric_type"] = cell_.metric_type;
    j["period_ms"] = cell_.period_ms;
    j["duration_s"] = cell_.duration_s;
    j["map_file"] = cell_.map_file;
    j["realtime"] = cell_.period_ms > 0.0f;

    // Read live rather than stored: UEs come and go with the enabled knob.
    j["n_ues"] = (int)(ue_list_ != nullptr ? ue_list_->size() : 0);
    int enabled = 0;
    for (size_t i = 0; ue_list_ != nullptr && i < ue_list_->size(); i++)
        if ((*ue_list_)[i].is_enabled()) enabled++;
    j["n_ues_enabled"] = enabled;
    j["ues"] = nlohmann::json::array();
    for (size_t i = 0; ue_list_ != nullptr && i < ue_list_->size(); i++)
    {
        nlohmann::json item;
        item["index"] = (*ue_list_)[i].get_id();
        item["ue_id"] = (*ue_list_)[i].get_control_id();
        j["ues"].push_back(item);
    }
    // The apothem is a property of the scenario map, held by every UE's MapHandler.
    j["apothem_m"] = (ue_list_ != nullptr && !ue_list_->empty()) ? (*ue_list_)[0].get_apothem() : 0.0f;

    return j.dump();
}

std::string control_manager::read_object_events(const command &c, ack &a)
{
    nlohmann::json result;
    result["cursor"] = object_event_seq_;
    result["events"] = nlohmann::json::array();

    if (c.resync_events)
    {
        // One quiescent operation: the snapshot is the exact cumulative state from
        // which future deltas start, so there is no gap between a separate get and
        // re-arming the stream.
        std::vector<ue *> targets;
        for (size_t i = 0; ue_list_ != nullptr && i < ue_list_->size(); i++)
            targets.push_back(&(*ue_list_)[i]);
        result["snapshot"] = nlohmann::json::parse(read_state(targets));

        object_events_.clear();
        object_event_floor_ = object_event_seq_;
        object_event_gap_ = false;
        for (ue *u : targets) u->enable_object_events();
        object_events_enabled_ = true;
        result["cursor"] = object_event_seq_;
        return result.dump();
    }

    if (object_event_gap_)
    {
        ack_error e; e.key = "after";
        e.reason = "event backlog overflow; resync with events resync:true";
        a.errors.push_back(e);
        return result.dump();
    }

    if (!object_events_enabled_)
    {
        if (c.after != 0)
        {
            ack_error e; e.key = "after"; e.reason = "first events cursor must be 0";
            a.errors.push_back(e);
            return result.dump();
        }

        // Subscription starts here, not retroactively. Existing get-only clients keep
        // paying no delta-accounting cost, and a client can switch explicitly by first
        // taking a full get and then arming events with cursor zero.
        for (size_t i = 0; ue_list_ != nullptr && i < ue_list_->size(); i++)
            (*ue_list_)[i].enable_object_events();
        object_events_enabled_ = true;
    }
    else
    {
        if (c.after < object_event_floor_)
        {
            ack_error e; e.key = "after"; e.reason = "cursor has already been acknowledged";
            a.errors.push_back(e);
            return result.dump();
        }
        if (c.after > object_event_seq_)
        {
            ack_error e; e.key = "after"; e.reason = "cursor is ahead of the event stream";
            a.errors.push_back(e);
            return result.dump();
        }

        // `after` is proof of consumption, not the cursor being requested. Keep newer
        // events until a later call acknowledges them; retrying this call is therefore
        // idempotent even if its previous reply was lost.
        while (!object_events_.empty() && object_events_.front().seq <= c.after)
            object_events_.pop_front();
        object_event_floor_ = c.after;
    }

    result["cursor"] = object_event_seq_;
    for (const object_event &e : object_events_)
    {
        nlohmann::json j;
        j["seq"] = e.seq;
        j["at_tti"] = e.at_tti;
        j["target"] = std::string("ue/") + std::to_string(e.ue_id);
        j["ue_id"] = e.ue_name;
        j["dir"] = e.tx_dir == TX_UL ? "ul" : "dl";
        j["tag"] = e.tag;
        // Zero fields carry no information and dominate tiny events on the wire.
        if (e.delivered_bytes > 0.0) j["delivered_bytes"] = e.delivered_bytes;
        if (e.expired_bytes > 0.0) j["expired_bytes"] = e.expired_bytes;
        if (e.queue_dropped_bytes > 0.0) j["queue_dropped_bytes"] = e.queue_dropped_bytes;
        if (e.radio_dropped_bytes > 0.0) j["radio_dropped_bytes"] = e.radio_dropped_bytes;
        if (e.ce_bytes > 0.0) j["ce_bytes"] = e.ce_bytes;
        result["events"].push_back(j);
    }

    if (c.include_state)
    {
        nlohmann::json state = nlohmann::json::array();
        for (size_t i = 0; ue_list_ != nullptr && i < ue_list_->size(); i++)
        {
            nlohmann::json j;
            j["target"] = std::string("ue/") + std::to_string((*ue_list_)[i].get_id());
            j["ue_id"] = (*ue_list_)[i].get_control_id();
            j["dl"] = direction_state((*ue_list_)[i], TX_DL, false);
            j["ul"] = direction_state((*ue_list_)[i], TX_UL, false);
            j["mobility"] = {
                {"pos_x_m", (*ue_list_)[i].mobility().x()},
                {"pos_y_m", (*ue_list_)[i].mobility().y()},
                {"speed_kmh", (*ue_list_)[i].mobility().get_speed() * 3.6}
            };
            state.push_back(j);
        }
        result["state"] = state;
    }

    return result.dump();
}

std::string control_manager::read_state(const std::vector<ue *> &targets) const
{
    const param_registry &reg = param_registry::instance();
    nlohmann::json out = nlohmann::json::array();
    for (size_t t = 0; t < targets.size(); t++)
    {
        nlohmann::json j;
        j["target"] = std::string("ue/") + std::to_string(targets[t]->get_id());
        j["ue_id"] = targets[t]->get_control_id();
        const std::vector<param_entry> &entries = reg.entries();
        for (size_t i = 0; i < entries.size(); i++)
        {
            if (!entries[i].read) continue;
            if (entries[i].type == param_type::boolean) j[entries[i].name] = entries[i].read(*targets[t]) != 0.0;
            else j[entries[i].name] = entries[i].read(*targets[t]);
        }

        nlohmann::json state;
        state["dl"] = direction_state(*targets[t], TX_DL);
        state["ul"] = direction_state(*targets[t], TX_UL);
        state["pkt_size_bits"] = targets[t]->get_pkt_size(TX_DL);
        j["state"] = state;

        out.push_back(j);
    }
    return out.dump();
}

void control_manager::refill_rate_buckets()
{
    if (!any_rate_cap_) return;

    for (size_t i = 0; i < ue_list_->size(); i++)
    {
        ue_overrides &c = (*ue_list_)[i].overrides();
        for (int d = 0; d < 2; d++)
        {
            if (c.rmax_bps[d] <= 0.0f) continue;
            const float cap = c.rmax_bps[d] * TTI_S;
            c.rmax_tokens[d] = std::min(c.rmax_tokens[d] + cap, cap);
        }
    }
}
