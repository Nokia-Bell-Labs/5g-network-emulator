/**********************************************
* Copyright 2026 Nokia
* Licensed under the BSD 3-Clause Clear License
* SPDX-License-Identifier: BSD-3-Clause-Clear
**********************************************/

#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <fstream>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <unordered_map>
#include <vector>

#include <utils/control/cell_info.h>
#include <utils/control/command.h>
#include <utils/control/control_config.h>
#include <utils/control/control_transport.h>
#include <utils/run_status.h>

class ue;

//--------------------------------------------------------------------------------------------------
// control_manager(): the runtime control plane. Member of simulator, never a singleton.
//
// tick() is the only place where control state is written, and it runs as the first
// statement of simulator::step(), before mac_l.step(). At that instant both thread pools
// are parked on their condition variables (mac_layer::step and ue_handler::step both end
// in wait_threads()), so mutating UE state there needs no atomics on the mutated fields
// and no locks on the readers.
//
// Applying a command anywhere else -- from the transport thread, or from inside a
// parallel phase -- would race against grid_dl and grid_ul, which walk the same ue_list
// concurrently.
//--------------------------------------------------------------------------------------------------
class control_manager
{
public:
    control_manager();
    ~control_manager();

    // cell describes the run and is answered verbatim by a get on the cell target; its
    // period_ms decides whether the barrier is available and its metric_type is used to warn
    // that priority does nothing under round robin.
    void init(const control_config &cfg, std::vector<ue> *ue_list, const cell_info &cell);

    void tick(double sim_t, std::int64_t tti);

    void stop();
    void interrupt();

    bool is_enabled() const { return enabled_; }
    bool stop_requested() const { return stopping_.load(); }
    run_stop_reason stop_reason() const { return stop_reason_; }
    std::int64_t credit_until_tti() const { return credit_until_tti_; }
    bool barrier_mode() const { return mode_ == mode_t::barrier; }

private:
    enum class mode_t { async, barrier };
    enum class on_timeout_t { cont, abort };
    enum class on_peer_loss_t { cont, abort };

    void wait_for_credit(std::int64_t tti);
    void apply_grant(const command &c, ack &a);
    void write_journal(const command &c, double sim_t, std::int64_t tti);
    void publish_metrics(std::int64_t tti);
    void drain_transport();
    void collect_object_events(std::int64_t tti);
    void apply_due(double sim_t, std::int64_t tti);
    void apply(const command &c, double sim_t, std::int64_t tti);
    void refill_rate_buckets();

    // Validates every key of the message before touching anything, so that a ue/* with
    // one bad value does not leave the system half applied.
    bool validate(const command &c, std::vector<ue *> &targets, ack &a);
    bool resolve_target(const std::string &target, std::vector<ue *> &out, ack &a);
    std::string read_state(const std::vector<ue *> &targets) const;
    std::string read_cell_state() const;
    std::string read_object_events(const command &c, ack &a);
    void warn_priority_under_rr();

private:
    struct scheduled
    {
        std::int64_t at_tti;
        std::uint64_t seq;   // keeps FIFO order among commands due at the same TTI
        command cmd;
    };
    struct scheduled_later
    {
        bool operator()(const scheduled &a, const scheduled &b) const
        {
            if (a.at_tti != b.at_tti) return a.at_tti > b.at_tti;
            return a.seq > b.seq;
        }
    };
    struct object_event
    {
        std::uint64_t seq = 0;
        std::int64_t at_tti = -1;
        int ue_id = -1;
        std::string ue_name;
        int tx_dir = -1;
        std::uint32_t tag = 0;
        double delivered_bytes = 0.0;
        double expired_bytes = 0.0;
        double queue_dropped_bytes = 0.0;
        double radio_dropped_bytes = 0.0;
        double ce_bytes = 0.0;
    };

private:
    bool enabled_ = false;
    std::vector<ue *> ue_index_;          // by ue id, stable after ue_handler::init()
    std::unordered_map<std::string, ue *> ue_name_index_;
    std::vector<ue> *ue_list_ = nullptr;
    std::unique_ptr<control_transport> transport_;
    bool transport_open_ = false;

    std::priority_queue<scheduled, std::vector<scheduled>, scheduled_later> sched_;
    std::uint64_t seq_ = 0;

    // Set as soon as a rate cap is configured on any UE, so that a run without caps does
    // not pay for the refill walk.
    bool any_rate_cap_ = false;
    bool ranks_dirty_ = false;

    mode_t mode_ = mode_t::async;
    on_timeout_t on_timeout_ = on_timeout_t::cont;
    on_peer_loss_t on_peer_loss_ = on_peer_loss_t::abort;
    std::chrono::milliseconds timeout_{30000};
    std::int64_t credit_until_tti_ = -1;
    std::uint64_t credit_generation_ = 0;
    std::atomic<bool> stopping_{false};
    run_stop_reason stop_reason_ = run_stop_reason::none;
    std::mutex mtx_;
    std::condition_variable cv_;

    std::ofstream journal_;

    // Per-window counters of the control plane itself, so that "what changed" and "what
    // happened" can be correlated in the same data.
    int applied_in_tick_ = 0;
    int rejected_in_tick_ = 0;
    int blocked_ttis_ = 0;
    double blocked_ms_ = 0.0;
    std::int64_t last_applied_tti_ = -1;

    int max_cmds_per_tick_ = 256;
    bool warned_rr_priority_ = false;
    cell_info cell_;

    // A single control peer consumes a replayable stream of per-tag counter deltas.
    // Events are retained until that peer proves it consumed them by sending their
    // cursor back as `after`, so retrying after a lost reply cannot lose feedback.
    bool object_events_enabled_ = false;
    bool object_event_gap_ = false;
    std::uint64_t object_event_seq_ = 0;
    std::uint64_t object_event_floor_ = 0;
    std::deque<object_event> object_events_;
    size_t max_object_events_ = 65536;
};
