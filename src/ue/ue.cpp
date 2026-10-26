/**********************************************
 * Copyright 2022 Nokia
 * Licensed under the BSD 3-Clause Clear License
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 **********************************************/

#include <ue/ue.h>
#include <cassert>
#include <chrono>
#include "utils/monitoring/monitoring_manager.h"

namespace
{
std::string escape_influx_tag(const std::string &value)
{
    std::string out;
    out.reserve(value.size());
    for(char c : value)
    {
        if(c == ',' || c == ' ' || c == '=') out.push_back('\\');
        out.push_back(c);
    }
    return out;
}

std::string escape_influx_string_field(const std::string &value)
{
    std::string out;
    out.reserve(value.size());
    for(char c : value)
    {
        if(c == '\\' || c == '"') out.push_back('\\');
        out.push_back(c);
    }
    return out;
}

metric_field make_metric_field(double value, field_aggregation aggregation)
{
    metric_field field;
    field.value = value;
    field.aggregation = aggregation;
    return field;
}

std::string make_text_log_line(int ue_id, const std::string &line)
{
    const auto now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    std::string lp = "emulator_log";
    lp += ",component=ue";
    lp += ",ue_id=" + escape_influx_tag(std::to_string(ue_id));
    lp += " message=\"" + escape_influx_string_field(line) + "\"";
    lp += " " + std::to_string(now_ns);
    return lp;
}

std::string tx_dir_tag(int tx_dir)
{
    return tx_dir == TX_DL ? "dl" : "ul";
}

packet_handler_config make_pdcp_packet_config(int ue_type,
                                              int tx_dir,
                                              int queue_num,
                                              int ue_id,
                                              std::chrono::microseconds *init_t,
                                              traffic_config traffic_c,
                                              pdcp_config pdcp_c,
                                              dualpi2_config l4s_c,
                                              bool log_traffic,
                                              bool log_quality)
{
    packet_handler_config cfg;
    cfg.ue_type = ue_type;
    cfg.tx_dir = tx_dir;
    cfg.queue_num = queue_num;
    cfg.ue_id = ue_id;
    cfg.init_t = init_t;
    cfg.traffic_c = traffic_c;
    cfg.pdcp_c = pdcp_c;
    cfg.l4s_c = l4s_c;
    cfg.log_traffic = log_traffic;
    cfg.log_quality = log_quality;
    return cfg;
}
}

ue::ue(int _id,
       ue_config ue_c,
       scenario_config _scenario_c,
       phy_enb_config _phy_enb_config,
       pdcp_config _pdcp_config_ul,
       pdcp_config _pdcp_config_dl,
       harq_config _harq_config,
       int ue_type,
       std::chrono::microseconds *init_t,
       bool _stochastics)
     :  map(_scenario_c.map_file),
        pdcp_dl(make_pdcp_packet_config(ue_type, TX_DL, ue_c.dl_queue_n, _id, init_t, ue_c.traffic_c, _pdcp_config_dl, ue_c.l4s_c, ue_c.log_traffic, ue_c.log_quality), ue_c.log_traffic || ue_c.log_quality || (monitoring_manager::instance().is_enabled() && (monitoring_manager::instance().get_config().emit_ue_pdcp || monitoring_manager::instance().get_config().emit_l4s))),
        pdcp_ul(make_pdcp_packet_config(ue_type, TX_UL, ue_c.ul_queue_n, _id, init_t, ue_c.traffic_c, _pdcp_config_ul, ue_c.l4s_c, ue_c.log_traffic, ue_c.log_quality), ue_c.log_traffic || ue_c.log_quality || (monitoring_manager::instance().is_enabled() && (monitoring_manager::instance().get_config().emit_ue_pdcp || monitoring_manager::instance().get_config().emit_l4s))),
        phy_dl(TX_DL, _id, _scenario_c, ue_c.get_phy_config(), _phy_enb_config, _stochastics, ue_c.log_quality || (monitoring_manager::instance().is_enabled() && monitoring_manager::instance().get_config().emit_ue_phy)),
        phy_ul(TX_UL, _id, _scenario_c, ue_c.get_phy_config(), _phy_enb_config, _stochastics, ue_c.log_quality || (monitoring_manager::instance().is_enabled() && monitoring_manager::instance().get_config().emit_ue_phy)),
        mobility_m(_id, ue_c.mobility_c, _scenario_c.type, map.getMaxApothem()),
        phy_s(_id, ue_c, _scenario_c, _phy_enb_config)
{
    id = _id;
    control_id = ue_c.control_id.empty() ? std::to_string(id) : ue_c.control_id;
    // The knob is absolute: it starts at the .ini value and replaces it from then on.
    ctl.priority = ue_c.priority;
    delta_metric = ue_c.delta_metric;
    delay_t_metric = ue_c.delay_t_metric;
    beta_metric = ue_c.beta_metric;

    verbosity = ue_c.log_ue;
    log_ue = ue_c.log_ue && ue_c.log_id != "";
    if (log_ue)
    {
        ue_log.init("logs/" + ue_c.log_id + "/ue/", "ue_log_" + std::to_string(id), ue_c.log_freq);
    }
    log_mobility = ue_c.log_mobility && log_ue;
    log_traffic = ue_c.log_traffic && log_ue;
    log_quality = ue_c.log_quality && log_ue;
    n_antennas = std::max(std::min(MAX_N_ANTENNAS, ue_c.ue_m.n_antennas), MIN_N_ANTENNAS);

    pdcp_ul.set_pkt_delay_budget(ue_c.pkt_delay_budget);
    pdcp_dl.set_pkt_delay_budget(ue_c.pkt_delay_budget);
    phy_cqi_period = ue_c.ue_m.cqi_period;
    phy_ri_period = ue_c.ue_m.ri_period;
    phy_freq = _phy_enb_config.frequency;
    phy_speed = ue_c.get_phy_config().max_speed;
    phy_doppler_f = phy_freq * phy_speed / LIGHTSPEED;
    phy_dl.init(
        _phy_enb_config.n_rbgs,
        _phy_enb_config.bandwidth,
        _phy_enb_config.n_carrier_prbs);
    phy_ul.init(
        _phy_enb_config.n_rbgs,
        _phy_enb_config.bandwidth,
        _phy_enb_config.n_carrier_prbs);
    pdcp_ul.init(_harq_config.mod, _harq_config.layers, _harq_config.log_units);
    pdcp_dl.init(_harq_config.mod, _harq_config.layers, _harq_config.log_units);
}

void ue::init()
{
    init_logger();
}

void ue::init_logger()
{
    if (log_quality)
    {
        phy_dl.set_logger(&ue_log);
        phy_ul.set_logger(&ue_log);
    }

    if (log_ue)
    {
        ue_log.set_monitoring_callback([this](const std::string &line){
            monitoring_manager::instance().send_text_line(make_text_log_line(id, line));
        });
    }
}

phy_layer& ue::phy(int tx_dir)
{
    assert(tx_dir == TX_DL || tx_dir == TX_UL);
    return tx_dir == TX_DL ? phy_dl : phy_ul;
}

const phy_layer& ue::phy(int tx_dir) const
{
    assert(tx_dir == TX_DL || tx_dir == TX_UL);
    return tx_dir == TX_DL ? phy_dl : phy_ul;
}

pdcp_layer& ue::pdcp(int tx_dir)
{
    assert(tx_dir == TX_DL || tx_dir == TX_UL);
    return tx_dir == TX_DL ? pdcp_dl : pdcp_ul;
}

const pdcp_layer& ue::pdcp(int tx_dir) const
{
    assert(tx_dir == TX_DL || tx_dir == TX_UL);
    return tx_dir == TX_DL ? pdcp_dl : pdcp_ul;
}

float ue::get_metric(int tx_dir, int f_index, int n_enabled)
{
    return phy(tx_dir).get_metric(f_index, n_enabled, ctl.rr_rank, ctl.priority);
}

float ue::get_tp(int tx_dir, int f_index)
{
    return phy(tx_dir).get_tp(f_index);
}

int ue::get_cqi(int tx_dir, int f_index)
{
    return phy(tx_dir).get_cqi(f_index);
}

float ue::get_eff(int tx_dir, int f_index)
{
    return phy(tx_dir).get_eff(f_index);
}

int ue::get_mcs(int tx_dir, int f_index)
{
    return phy(tx_dir).get_mcs(f_index);
}

int ue::get_ri(int tx_dir)
{
    return phy(tx_dir).get_ri();
}

float ue::get_sinr(int tx_dir, int f_index)
{
    return phy(tx_dir).get_sinr(f_index);
}

float ue::get_linear_sinr(int tx_dir, int f_index)
{
    return phy(tx_dir).get_linear_sinr(f_index);
}

float ue::get_mean_sinr(int tx_dir)
{
    return phy(tx_dir).get_mean_sinr();
}

float ue::get_avg_tp(int tx_dir)
{
    return pdcp(tx_dir).get_tp(false);
}

bool ue::get_traffic_verbosity()
{
    return verbosity & log_traffic;
}

float ue::get_avg_l(int tx_dir)
{
    return pdcp(tx_dir).get_latency(false);
}

float ue::get_oldest_timestamp(int tx_dir)
{
    return pdcp(tx_dir).get_oldest_timestamp();
}

bool ue::has_packets(int tx_dir)
{
    return pdcp(tx_dir).has_pkts();
}

// Every UE is asked, including the disabled ones. Returning early on !ctl.enabled looks
// free and is not: each UE keeps its own round robin cursor and they stay in step only
// because all of them are asked for the same sequence of f. A skipped UE would fall out
// of step and break the rotation, silently, when it is enabled again.
schedule_candidate ue::get_schedule_candidate(int tx_dir, int f_index, int n_enabled, int ue_index)
{
    schedule_candidate candidate;
    candidate.ue_index = ue_index;
    candidate.ue_id = id;
    candidate.bits_per_symbol = phy(tx_dir).get_tp(f_index);
    candidate.metric = phy(tx_dir).get_metric(f_index, n_enabled, ctl.rr_rank, ctl.priority);
    candidate.has_data = ctl.enabled
                      && pdcp(tx_dir).has_pkts()
                      && candidate.bits_per_symbol > 0
                      && (ctl.rmax_bps[tx_dir] <= 0.0f || ctl.rmax_tokens[tx_dir] > 0.0f);
    return candidate;
}

void ue::set_enabled(bool on)
{
    if (ctl.enabled == on) return;
    ctl.enabled = on;
    if (!on)
    {
        pdcp_dl.drop_all();
        pdcp_ul.drop_all();
        phy_dl.reset_scheduler_state();
        phy_ul.reset_scheduler_state();
        scheduler_effective_bits[TX_DL] = 0.0f;
        scheduler_effective_bits[TX_UL] = 0.0f;
        scheduler_active[TX_DL] = false;
        scheduler_active[TX_UL] = false;
        scheduler_current_service_gap_ttis[TX_DL] = 0;
        scheduler_current_service_gap_ttis[TX_UL] = 0;
        scheduler_max_service_gap_ttis[TX_DL] = 0;
        scheduler_max_service_gap_ttis[TX_UL] = 0;
    }
}

void ue::update_pos()
{
    mobility_m.update_pos(current_t);
    if (log_mobility)
        ue_log.log_partial("x:{} y:{} ", mobility_m.x(), mobility_m.y());
}

void ue::emit_mobility_monitoring()
{
    monitoring_manager &monitoring = monitoring_manager::instance();
    if(!monitoring.is_enabled() || !monitoring.get_config().emit_ue_mobility) return;

    metric_point point;
    point.measurement = "ue_mobility";
    point.tags["ue_id"] = std::to_string(id);
    point.fields["x"] = make_metric_field(mobility_m.x(), field_aggregation::last);
    point.fields["y"] = make_metric_field(mobility_m.y(), field_aggregation::last);
    point.fields["distance_m"] = make_metric_field(mobility_m.get_distance(), field_aggregation::last);
    monitoring.publish(point);
}


float ue::handle_pkt(float bits, int tx_dir, int f_index)
{
    // The cap is on the bits granted over the air, retransmissions and padding included.
    // Tokens may go negative: the UE stops being a candidate until the per-TTI refill
    // brings them back, which keeps the long run rate at rmax without splitting an RBG.
    float eff_tp = pdcp(tx_dir).handle_pkt(
        bits,
        phy(tx_dir).get_mcs(f_index),
        phy(tx_dir).get_sinr(f_index),
        mobility_m.get_distance(),
        phy(tx_dir).get_ri());
    if (ctl.rmax_bps[tx_dir] > 0.0f)
        ctl.rmax_tokens[tx_dir] -=
            static_cast<float>(
                pdcp(tx_dir).last_charged_grant_bits());
    scheduler_effective_bits[tx_dir] += eff_tp;
    return eff_tp;
}

void ue::finalize_ul_allocation(int allocated_prbs)
{
    phy_ul.finalize_ul_allocation(allocated_prbs);
}

void ue::prepare_scheduler_tti(int tx_dir, bool active)
{
    scheduler_active[tx_dir] = active;
    phy(tx_dir).prepare_scheduler_tti(active);
}

void ue::record_provisional_scheduler_service(int tx_dir, float scheduled_bits)
{
    phy(tx_dir).record_provisional_service(scheduled_bits);
}

void ue::commit_scheduler_tti()
{
    for (int tx_dir = TX_DL; tx_dir <= TX_UL; tx_dir++)
    {
        if (!scheduler_active[tx_dir])
        {
            scheduler_current_service_gap_ttis[tx_dir] = 0;
            continue;
        }
        if (scheduler_effective_bits[tx_dir] > 0.0f)
        {
            scheduler_current_service_gap_ttis[tx_dir] = 0;
        }
        else
        {
            scheduler_current_service_gap_ttis[tx_dir]++;
            scheduler_max_service_gap_ttis[tx_dir] =
                std::max(
                    scheduler_max_service_gap_ttis[tx_dir],
                    scheduler_current_service_gap_ttis[tx_dir]);
        }
    }
    phy_dl.update_scheduler_state(
        scheduler_effective_bits[TX_DL], scheduler_active[TX_DL]);
    phy_ul.update_scheduler_state(
        scheduler_effective_bits[TX_UL], scheduler_active[TX_UL]);
    scheduler_effective_bits[TX_DL] = 0.0f;
    scheduler_effective_bits[TX_UL] = 0.0f;
    scheduler_active[TX_DL] = false;
    scheduler_active[TX_UL] = false;
}

float ue::get_delay_t()
{
    return delay_t_metric;
}

float ue::get_delta()
{
    return delta_metric;
}

int ue::get_id()
{
    return id;
}

void ue::update_pdcp()
{
    pdcp_dl.step(current_t);
    pdcp_ul.step(current_t);
    pdcp_dl.release();
    pdcp_ul.release();
    last_l4s_ul_interval_stats = pdcp_ul.get_l4s_interval_stats();
    last_l4s_dl_interval_stats = pdcp_dl.get_l4s_interval_stats();
    if (log_traffic && ue_log.ready())
    {
        ue_log.log_partial("rul:{} rdl:{} ", pdcp_ul.get_tp(true), pdcp_dl.get_tp(true));
        ue_log.log_partial("lul:{} ldl:{} ", pdcp_ul.get_latency(true), pdcp_dl.get_latency(true));
        ue_log.log_partial("ilul:{} ildl:{} ", pdcp_ul.get_ip_latency(true), pdcp_dl.get_ip_latency(true));
        ue_log.log_partial("eul:{} edl:{} ", pdcp_ul.get_error(true), pdcp_dl.get_error(true));
        const dualpi2_stats &l4s_ul = last_l4s_ul_interval_stats;
        const dualpi2_stats &l4s_dl = last_l4s_dl_interval_stats;
        ue_log.log_partial("ceul:{} cedl:{} cebul:{} cebdl:{} ", l4s_ul.ce_packets, l4s_dl.ce_packets, l4s_ul.ce_bits, l4s_dl.ce_bits);
        ue_log.log_partial("aqmdul:{} aqmddl:{} aqmdbul:{} aqmdbdl:{} ", l4s_ul.aqm_drops, l4s_dl.aqm_drops, l4s_ul.aqm_drop_bits, l4s_dl.aqm_drop_bits);
        ue_log.log_partial("qlul:{} qldl:{} qcul:{} qcdl:{} ", l4s_ul.l4s_queue_size, l4s_dl.l4s_queue_size, l4s_ul.classic_queue_size, l4s_dl.classic_queue_size);
        ue_log.log_partial("plul:{} pldl:{} pcul:{} pcdl:{} pclul:{} pcldl:{} ", l4s_ul.p_l, l4s_dl.p_l, l4s_ul.p_c, l4s_dl.p_c, l4s_ul.p_cl, l4s_dl.p_cl);
        pdcp_queue_status ul_status = pdcp_ul.get_queue_status();
        pdcp_queue_status dl_status = pdcp_dl.get_queue_status();
        ue_log.log_partial(
            "admbul:{} admbdl:{} delbul:{} delbdl:{} expbul:{} expbdl:{} "
            "qdropbul:{} qdropbdl:{} rdropbul:{} rdropbdl:{} "
            "rtxbul:{} rtxbdl:{} pendbul:{} pendbdl:{} cresbul:{} cresbdl:{} ",
            ul_status.admitted_bits_total, dl_status.admitted_bits_total,
            pdcp_ul.delivered_bits_total(), pdcp_dl.delivered_bits_total(),
            pdcp_ul.expired_bits_total(), pdcp_dl.expired_bits_total(),
            pdcp_ul.queue_dropped_bits_total(), pdcp_dl.queue_dropped_bits_total(),
            pdcp_ul.radio_dropped_bits_total(), pdcp_dl.radio_dropped_bits_total(),
            pdcp_ul.retransmitted_bits_total(), pdcp_dl.retransmitted_bits_total(),
            ul_status.pending_bits_total, dl_status.pending_bits_total,
            ul_status.conservation_residual_bits, dl_status.conservation_residual_bits);
        ue_log.log_partial(
            "harqqul:{} harqqdl:{} harqhwul:{} harqhwddl:{} "
            "harqageul:{} harqagedl:{} harqretryul:{} harqretrydl:{} ",
            ul_status.harq_size, dl_status.harq_size,
            ul_status.harq_high_water_blocks, dl_status.harq_high_water_blocks,
            ul_status.harq_oldest_age, dl_status.harq_oldest_age,
            ul_status.harq_oldest_retry_ordinal,
            dl_status.harq_oldest_retry_ordinal);
        if(ul_status.nfqueue_queue_num >= 0 || dl_status.nfqueue_queue_num >= 0)
        {
            ue_log.log_partial(
                "nfqrecvul:{} nfqrecvdl:{} nfqrlsul:{} nfqrlsdl:{} "
                "nfqcewul:{} nfqcewdl:{} nfqdropul:{} nfqdropdl:{} nfqrfailul:{} nfqrfaildl:{} "
                "nfqsfailul:{} nfqsfaildl:{} ",
                ul_status.nfqueue_total_recv, dl_status.nfqueue_total_recv,
                ul_status.nfqueue_total_rlsd, dl_status.nfqueue_total_rlsd,
                ul_status.nfqueue_ce_rewrite_packets, dl_status.nfqueue_ce_rewrite_packets,
                ul_status.nfqueue_drop_packets, dl_status.nfqueue_drop_packets,
                ul_status.nfqueue_recv_fails, dl_status.nfqueue_recv_fails,
                ul_status.nfqueue_rlsd_fails, dl_status.nfqueue_rlsd_fails
            );
        }
    }
}

void ue::emit_pdcp_monitoring()
{
    monitoring_manager &monitoring = monitoring_manager::instance();
    if(!monitoring.is_enabled() || !monitoring.get_config().emit_ue_pdcp) return;

    const auto ts_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    metric_point ul_point;
    ul_point.measurement = "ue_pdcp";
    ul_point.tags["ue_id"] = std::to_string(id);
    ul_point.tags["tx_dir"] = tx_dir_tag(TX_UL);
    ul_point.ts_ns = ts_ns;
    ul_point.fields["throughput_mbps_mean"] = make_metric_field(pdcp_ul.get_tp(true), field_aggregation::mean);
    ul_point.fields["generated_mbps_sum"] = make_metric_field(pdcp_ul.get_generated(true), field_aggregation::sum);
    ul_point.fields["generated_packets_sum"] = make_metric_field(pdcp_ul.get_generated_packets(true), field_aggregation::sum);
    ul_point.fields["error_mbps_sum"] = make_metric_field(pdcp_ul.get_error(true), field_aggregation::sum);
    ul_point.fields["latency_s_mean"] = make_metric_field(pdcp_ul.get_latency(true), field_aggregation::mean);
    ul_point.fields["ip_latency_s_mean"] = make_metric_field(pdcp_ul.get_ip_latency(true), field_aggregation::mean);
    const pdcp_queue_status ul_status = pdcp_ul.get_queue_status();
    ul_point.tags["harq_model"] = ul_status.harq_model;
    ul_point.fields["harq_queue_blocks_last"] = make_metric_field(ul_status.harq_size, field_aggregation::last);
    ul_point.fields["harq_queue_high_water_blocks_last"] = make_metric_field(ul_status.harq_high_water_blocks, field_aggregation::last);
    ul_point.fields["harq_oldest_age_s_last"] = make_metric_field(ul_status.harq_oldest_age, field_aggregation::last);
    ul_point.fields["harq_oldest_retry_ordinal_last"] = make_metric_field(ul_status.harq_oldest_retry_ordinal, field_aggregation::last);
    ul_point.fields["retransmitted_bits_total_last"] = make_metric_field(static_cast<double>(ul_status.retransmitted_bits_total), field_aggregation::last);
    ul_point.fields["radio_dropped_bits_total_last"] = make_metric_field(static_cast<double>(ul_status.radio_dropped_bits_total), field_aggregation::last);
    ul_point.fields["charged_grant_bits_last"] = make_metric_field(static_cast<double>(ul_status.last_charged_grant_bits), field_aggregation::last);
    monitoring.publish(ul_point);

    metric_point dl_point;
    dl_point.measurement = "ue_pdcp";
    dl_point.tags["ue_id"] = std::to_string(id);
    dl_point.tags["tx_dir"] = tx_dir_tag(TX_DL);
    dl_point.ts_ns = ts_ns;
    dl_point.fields["throughput_mbps_mean"] = make_metric_field(pdcp_dl.get_tp(true), field_aggregation::mean);
    dl_point.fields["generated_mbps_sum"] = make_metric_field(pdcp_dl.get_generated(true), field_aggregation::sum);
    dl_point.fields["generated_packets_sum"] = make_metric_field(pdcp_dl.get_generated_packets(true), field_aggregation::sum);
    dl_point.fields["error_mbps_sum"] = make_metric_field(pdcp_dl.get_error(true), field_aggregation::sum);
    dl_point.fields["latency_s_mean"] = make_metric_field(pdcp_dl.get_latency(true), field_aggregation::mean);
    dl_point.fields["ip_latency_s_mean"] = make_metric_field(pdcp_dl.get_ip_latency(true), field_aggregation::mean);
    const pdcp_queue_status dl_status = pdcp_dl.get_queue_status();
    dl_point.tags["harq_model"] = dl_status.harq_model;
    dl_point.fields["harq_queue_blocks_last"] = make_metric_field(dl_status.harq_size, field_aggregation::last);
    dl_point.fields["harq_queue_high_water_blocks_last"] = make_metric_field(dl_status.harq_high_water_blocks, field_aggregation::last);
    dl_point.fields["harq_oldest_age_s_last"] = make_metric_field(dl_status.harq_oldest_age, field_aggregation::last);
    dl_point.fields["harq_oldest_retry_ordinal_last"] = make_metric_field(dl_status.harq_oldest_retry_ordinal, field_aggregation::last);
    dl_point.fields["retransmitted_bits_total_last"] = make_metric_field(static_cast<double>(dl_status.retransmitted_bits_total), field_aggregation::last);
    dl_point.fields["radio_dropped_bits_total_last"] = make_metric_field(static_cast<double>(dl_status.radio_dropped_bits_total), field_aggregation::last);
    dl_point.fields["charged_grant_bits_last"] = make_metric_field(static_cast<double>(dl_status.last_charged_grant_bits), field_aggregation::last);
    monitoring.publish(dl_point);
}

void ue::emit_l4s_monitoring()
{
    monitoring_manager &monitoring = monitoring_manager::instance();
    if(!monitoring.is_enabled() || !monitoring.get_config().emit_l4s) return;
    if(!pdcp_ul.using_l4s() && !pdcp_dl.using_l4s()) return;

    const auto ts_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    auto publish_l4s = [&](pdcp_layer &layer, int tx_dir) {
        if(!layer.using_l4s()) return;
        const dualpi2_stats &interval = (tx_dir == TX_UL) ? last_l4s_ul_interval_stats : last_l4s_dl_interval_stats;
        pdcp_queue_status status = layer.get_queue_status();

        metric_point point;
        point.measurement = "ue_l4s";
        point.tags["ue_id"] = std::to_string(id);
        point.tags["tx_dir"] = tx_dir_tag(tx_dir);
        point.ts_ns = ts_ns;
        point.fields["generated_packets_sum"] = make_metric_field(layer.get_generated_packets(true), field_aggregation::sum);
        point.fields["ce_packets_sum"] = make_metric_field(interval.ce_packets, field_aggregation::sum);
        point.fields["ce_bits_sum"] = make_metric_field(interval.ce_bits, field_aggregation::sum);
        point.fields["aqm_drops_sum"] = make_metric_field(interval.aqm_drops, field_aggregation::sum);
        point.fields["aqm_drop_bits_sum"] = make_metric_field(interval.aqm_drop_bits, field_aggregation::sum);
        point.fields["l4s_queue_packets_last"] = make_metric_field(status.l4s_queue_size, field_aggregation::last);
        point.fields["classic_queue_packets_last"] = make_metric_field(status.classic_queue_size, field_aggregation::last);
        point.fields["l4s_queue_bits_last"] = make_metric_field(status.l4s_queue_bits, field_aggregation::last);
        point.fields["classic_queue_bits_last"] = make_metric_field(status.classic_queue_bits, field_aggregation::last);
        point.fields["p_l_mean"] = make_metric_field(status.dualpi2_p_l, field_aggregation::mean);
        point.fields["p_c_mean"] = make_metric_field(status.dualpi2_p_c, field_aggregation::mean);
        point.fields["p_cl_mean"] = make_metric_field(status.dualpi2_p_cl, field_aggregation::mean);
        point.fields["nfqueue_queue_num_last"] = make_metric_field(status.nfqueue_queue_num, field_aggregation::last);
        point.fields["nfqueue_ce_rewrite_packets_last"] = make_metric_field(status.nfqueue_ce_rewrite_packets, field_aggregation::last);
        point.fields["nfqueue_drop_packets_last"] = make_metric_field(status.nfqueue_drop_packets, field_aggregation::last);
        point.fields["nfqueue_total_recv_last"] = make_metric_field(status.nfqueue_total_recv, field_aggregation::last);
        point.fields["nfqueue_total_rlsd_last"] = make_metric_field(status.nfqueue_total_rlsd, field_aggregation::last);
        point.fields["nfqueue_bytes_recv_last"] = make_metric_field(status.nfqueue_bytes_recv, field_aggregation::last);
        point.fields["nfqueue_recv_fails_last"] = make_metric_field(status.nfqueue_recv_fails, field_aggregation::last);
        point.fields["nfqueue_rlsd_fails_last"] = make_metric_field(status.nfqueue_rlsd_fails, field_aggregation::last);
        point.fields["final_accept_packets_sum"] = make_metric_field(status.final_accept_packets, field_aggregation::sum);
        point.fields["final_accept_ce_packets_sum"] = make_metric_field(status.final_accept_ce_packets, field_aggregation::sum);
        point.fields["final_drop_packets_sum"] = make_metric_field(status.final_drop_packets, field_aggregation::sum);
        monitoring.publish(point);
    };

    publish_l4s(pdcp_ul, TX_UL);
    publish_l4s(pdcp_dl, TX_DL);
}

void ue::emit_queue_monitoring()
{
    monitoring_manager &monitoring = monitoring_manager::instance();
    if(!monitoring.is_enabled() || !monitoring.get_config().emit_ue_queue) return;

    const auto ts_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    auto publish_queue = [&](pdcp_layer &layer, int tx_dir) {
        const bool using_l4s = layer.using_l4s();
        const dualpi2_stats &interval = (tx_dir == TX_UL) ? last_l4s_ul_interval_stats : last_l4s_dl_interval_stats;
        pdcp_queue_status status = layer.get_queue_status();

        metric_point point;
        point.measurement = "ue_queue";
        point.tags["ue_id"] = std::to_string(id);
        point.tags["tx_dir"] = tx_dir_tag(tx_dir);
        point.tags["queue_mode"] = using_l4s ? "l4s" : "legacy";
        point.tags["harq_model"] = status.harq_model;
        point.ts_ns = ts_ns;
        point.fields["generated_packets_sum"] = make_metric_field(layer.get_generated_packets(true), field_aggregation::sum);
        point.fields["using_l4s_last"] = make_metric_field(using_l4s ? 1 : 0, field_aggregation::last);
        point.fields["pkt_delay_budget_s_last"] = make_metric_field(status.pkt_delay_budget_s, field_aggregation::last);
        point.fields["ip_buffer_packets_last"] = make_metric_field(status.ip_buffer_size, field_aggregation::last);
        point.fields["capture_packets_last"] = make_metric_field(status.capture_size, field_aggregation::last);
        point.fields["release_packets_last"] = make_metric_field(status.release_size, field_aggregation::last);
        point.fields["release_high_water_packets_last"] = make_metric_field(status.release_high_water, field_aggregation::last);
        point.fields["harq_packets_last"] = make_metric_field(status.harq_size, field_aggregation::last);
        point.fields["harq_high_water_packets_last"] = make_metric_field(status.harq_high_water_blocks, field_aggregation::last);
        point.fields["harq_capacity_packets_last"] = make_metric_field(status.harq_capacity_blocks, field_aggregation::last);
        point.fields["ip_oldest_age_s_last"] = make_metric_field(status.ip_oldest_age, field_aggregation::last);
        point.fields["capture_oldest_age_s_last"] = make_metric_field(status.capture_oldest_age, field_aggregation::last);
        point.fields["release_oldest_age_s_last"] = make_metric_field(status.release_oldest_age, field_aggregation::last);
        point.fields["harq_oldest_age_s_last"] = make_metric_field(status.harq_oldest_age, field_aggregation::last);
        point.fields["harq_oldest_retry_ordinal_last"] = make_metric_field(status.harq_oldest_retry_ordinal, field_aggregation::last);
        point.fields["retransmitted_bits_total_last"] = make_metric_field(static_cast<double>(status.retransmitted_bits_total), field_aggregation::last);
        point.fields["radio_dropped_bits_total_last"] = make_metric_field(static_cast<double>(status.radio_dropped_bits_total), field_aggregation::last);
        point.fields["charged_grant_bits_last"] = make_metric_field(static_cast<double>(status.last_charged_grant_bits), field_aggregation::last);
        point.fields["admitted_bits_total_last"] = make_metric_field(static_cast<double>(status.admitted_bits_total), field_aggregation::last);
        point.fields["pending_bits_total_last"] = make_metric_field(static_cast<double>(status.pending_bits_total), field_aggregation::last);
        point.fields["conservation_residual_bits_last"] = make_metric_field(status.conservation_residual_bits, field_aggregation::last);
        point.fields["nfqueue_queue_num_last"] = make_metric_field(status.nfqueue_queue_num, field_aggregation::last);
        point.fields["nfqueue_ce_rewrite_packets_last"] = make_metric_field(status.nfqueue_ce_rewrite_packets, field_aggregation::last);
        point.fields["nfqueue_drop_packets_last"] = make_metric_field(status.nfqueue_drop_packets, field_aggregation::last);
        point.fields["nfqueue_total_recv_last"] = make_metric_field(status.nfqueue_total_recv, field_aggregation::last);
        point.fields["nfqueue_total_rlsd_last"] = make_metric_field(status.nfqueue_total_rlsd, field_aggregation::last);
        point.fields["nfqueue_bytes_recv_last"] = make_metric_field(status.nfqueue_bytes_recv, field_aggregation::last);
        point.fields["nfqueue_recv_fails_last"] = make_metric_field(status.nfqueue_recv_fails, field_aggregation::last);
        point.fields["nfqueue_rlsd_fails_last"] = make_metric_field(status.nfqueue_rlsd_fails, field_aggregation::last);
        point.fields["final_accept_packets_sum"] = make_metric_field(status.final_accept_packets, field_aggregation::sum);
        point.fields["final_accept_ce_packets_sum"] = make_metric_field(status.final_accept_ce_packets, field_aggregation::sum);
        point.fields["final_drop_packets_sum"] = make_metric_field(status.final_drop_packets, field_aggregation::sum);
        point.fields["ce_packets_sum"] = make_metric_field(interval.ce_packets, field_aggregation::sum);
        point.fields["ce_bits_sum"] = make_metric_field(interval.ce_bits, field_aggregation::sum);
        point.fields["aqm_drops_sum"] = make_metric_field(interval.aqm_drops, field_aggregation::sum);
        point.fields["aqm_drop_bits_sum"] = make_metric_field(interval.aqm_drop_bits, field_aggregation::sum);
        point.fields["l4s_queue_packets_last"] = make_metric_field(status.l4s_queue_size, field_aggregation::last);
        point.fields["classic_queue_packets_last"] = make_metric_field(status.classic_queue_size, field_aggregation::last);
        point.fields["l4s_queue_bits_last"] = make_metric_field(status.l4s_queue_bits, field_aggregation::last);
        point.fields["classic_queue_bits_last"] = make_metric_field(status.classic_queue_bits, field_aggregation::last);
        point.fields["p_l_mean"] = make_metric_field(status.dualpi2_p_l, field_aggregation::mean);
        point.fields["p_c_mean"] = make_metric_field(status.dualpi2_p_c, field_aggregation::mean);
        point.fields["p_cl_mean"] = make_metric_field(status.dualpi2_p_cl, field_aggregation::mean);
        monitoring.publish(point);
    };

    publish_queue(pdcp_ul, TX_UL);
    publish_queue(pdcp_dl, TX_DL);
}

void ue::add_ts()
{
    if (log_ue)
    {
        ue_log.log_partial("ts:{}", current_t);
        ue_log.flush();
    }
}

void ue::init_phy_update_rates(float _doppler_f, int _cqi_p, int _ri_p)
{
    if (_doppler_f > 0)
    {
        phy_tc = 0.423 / _doppler_f;
        float min_sinr_update = phy_tc / 0.001;
        int min_update = std::min(_cqi_p, _ri_p);
        if (min_sinr_update > min_update)
            phy_sinr_period = min_update;
        else
            phy_sinr_period = std::max(1, (int)floor(min_sinr_update));
    }
    else
    {
        phy_tc = -1;
        phy_sinr_period = std::min(_cqi_p, _ri_p);
    }
}

void ue::emit_phy_monitoring()
{
    monitoring_manager &monitoring = monitoring_manager::instance();
    if(!monitoring.is_enabled() || !monitoring.get_config().emit_ue_phy) return;

    const auto ts_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    metric_point dl_point;
    dl_point.measurement = "ue_phy";
    dl_point.tags["ue_id"] = std::to_string(id);
    dl_point.tags["tx_dir"] = tx_dir_tag(TX_DL);
    dl_point.ts_ns = ts_ns;
    dl_point.fields["sinr_db_mean"] = make_metric_field(phy_dl.get_mean_sinr(), field_aggregation::mean);
    dl_point.fields["rsrp_db_mean"] = make_metric_field(phy_dl.get_mean_rsrp(), field_aggregation::mean);
    dl_point.fields["cqi_mean"] = make_metric_field(phy_dl.get_mean_cqi(), field_aggregation::mean);
    dl_point.fields["mcs_mean"] = make_metric_field(phy_dl.get_mean_mcs(), field_aggregation::mean);
    dl_point.fields["eff_mean"] = make_metric_field(phy_dl.get_mean_eff(), field_aggregation::mean);
    dl_point.fields["ri_mean"] = make_metric_field(phy_dl.get_ri(), field_aggregation::mean);
    if (phy_dl.uses_throughput_history_scheduler())
    {
        dl_point.fields["throughput_average_bits_per_tti_last"] =
            make_metric_field(
                phy_dl.get_throughput_average_bits_per_tti(),
                field_aggregation::last);
        dl_point.fields["throughput_metric_mean"] =
            make_metric_field(
                phy_dl.get_mean_scheduler_metric(),
                field_aggregation::mean);
        dl_point.fields["throughput_weighted_metric_mean"] =
            make_metric_field(
                phy_dl.get_mean_scheduler_metric() * ctl.priority,
                field_aggregation::mean);
    }
    monitoring.publish(dl_point);

    metric_point ul_point;
    ul_point.measurement = "ue_phy";
    ul_point.tags["ue_id"] = std::to_string(id);
    ul_point.tags["tx_dir"] = tx_dir_tag(TX_UL);
    ul_point.ts_ns = ts_ns;
    ul_point.fields["sinr_db_mean"] = make_metric_field(phy_ul.get_mean_sinr(), field_aggregation::mean);
    ul_point.fields["rsrp_db_mean"] = make_metric_field(phy_ul.get_mean_rsrp(), field_aggregation::mean);
    ul_point.fields["cqi_mean"] = make_metric_field(phy_ul.get_mean_cqi(), field_aggregation::mean);
    ul_point.fields["mcs_mean"] = make_metric_field(phy_ul.get_mean_mcs(), field_aggregation::mean);
    ul_point.fields["eff_mean"] = make_metric_field(phy_ul.get_mean_eff(), field_aggregation::mean);
    ul_point.fields["ri_mean"] = make_metric_field(phy_ul.get_ri(), field_aggregation::mean);
    if (phy_ul.uses_throughput_history_scheduler())
    {
        ul_point.fields["throughput_average_bits_per_tti_last"] =
            make_metric_field(
                phy_ul.get_throughput_average_bits_per_tti(),
                field_aggregation::last);
        ul_point.fields["throughput_metric_mean"] =
            make_metric_field(
                phy_ul.get_mean_scheduler_metric(),
                field_aggregation::mean);
        ul_point.fields["throughput_weighted_metric_mean"] =
            make_metric_field(
                phy_ul.get_mean_scheduler_metric() * ctl.priority,
                field_aggregation::mean);
    }
    monitoring.publish(ul_point);
}

void ue::estimate_channel_state()
{

    const pos2d &pos = *mobility_m.get_pos();
    float distance = mobility_m.get_distance();

    init_phy_update_rates(phy_doppler_f, phy_cqi_period, phy_ri_period);

    if (!phy_init_o2i)
    {
        phy_o2i = phy_s.get_o2i();
        phy_init_o2i = true;
        phy_corr_distance = phy_s.get_correlation_distance();
    }

    bool update_sinr = phy_period_counter % phy_sinr_period == 0;
    if (update_sinr)
    {
        phy_c_dist = get_distance(pos, phy_prev_pos);
        if (phy_c_dist > phy_corr_distance || phy_update_cs)
        {
            phy_corr_distance = phy_s.get_correlation_distance();
            phy_update_cs = false;
            phy_prev_pos = pos;
        }

        
        phy_macro_fading = map.getMacroFadingValue(pos.x, pos.y);
        
    }

    phy(TX_DL).estimate_channel_state(distance, phy_s, phy_macro_fading, phy_o2i, TX_DL, pos, get_oldest_timestamp(TX_DL), get_avg_tp(TX_DL), current_t, ctl.sinr_offset_db[TX_DL]);
    phy(TX_UL).estimate_channel_state(distance, phy_s, phy_macro_fading, phy_o2i, TX_UL, pos, get_oldest_timestamp(TX_UL), get_avg_tp(TX_UL), current_t, ctl.sinr_offset_db[TX_UL]);
    phy_period_counter++;
}

void ue::step()
{
    if (!ctl.enabled)
    {
        // A detached UE is gone from the simulation, but its source still has to be
        // drained: for a real UE that is the netfilter queue, which would otherwise back
        // up in the kernel, and for a simulated one it keeps the traffic generator's
        // random stream advancing, so that detaching a UE does not shift its own
        // realization. Everything ingested is dropped on the spot.
        pdcp_dl.step(current_t);
        pdcp_ul.step(current_t);
        pdcp_dl.drop_all();
        pdcp_ul.drop_all();
        return;
    }

    update_pdcp();
    update_pos();
    emit_mobility_monitoring();
    if (log_traffic && ue_log.ready())
    {
        ue_log.log_partial("gul:{} gdl:{} ", pdcp_ul.get_generated(true), pdcp_dl.get_generated(true));
    }
    emit_pdcp_monitoring();
    emit_queue_monitoring();
    emit_l4s_monitoring();
    if (log_ue)
        add_ts();

    estimate_channel_state();
    emit_phy_monitoring();

    return;
}

void ue::print_traffic()
{
    if (verbosity >= 1)
    {
        LOG_INFO_I("ue::print_traffic") << " UE: " << id << " distance: " << mobility_m.get_distance() << END();
        LOG_INFO_I("ue::print_traffic") << " UE: " << id << " Mbps - UL gen. rate " << pdcp_ul.get_generated(false) << " Mbps - DL gen. rate " << pdcp_dl.get_generated(false) << END();
        LOG_INFO_I("ue::print_traffic") << " UE: " << id << " Mbps - UL error rate " << pdcp_ul.get_error(false) << " Mbps - DL error rate " << pdcp_dl.get_error(false) << END();
        LOG_INFO_I("ue::print_traffic") << " " << id << " Mbps - UL cons. rate " << pdcp_ul.get_tp(false) << " Mbps - DL cons. rate " << pdcp_dl.get_tp(false) << END();
        LOG_INFO_I("ue::print_traffic") << " " << id << " Mbps - UL latency " << pdcp_ul.get_latency(false) << " s - DL latency " << pdcp_dl.get_latency(false) << END();
        LOG_INFO_I("ue::print_traffic") << " " << id << " Mbps - UL IP latency " << pdcp_ul.get_ip_latency(false) << " s - DL IP latency " << pdcp_dl.get_ip_latency(false) << END();

        auto ul_status = pdcp_ul.get_queue_status();
        auto dl_status = pdcp_dl.get_queue_status();

        LOG_INFO_I("ue::print_traffic") << " UL IP buf: " << ul_status.ip_buffer_size << " pkts (oldest UID " << ul_status.ip_oldest_uid << ", delay " << ul_status.ip_oldest_age << " s)" << END();
        LOG_INFO_I("ue::print_traffic") << " DL IP buf: " << dl_status.ip_buffer_size << " pkts (oldest UID " << dl_status.ip_oldest_uid << ", delay " << dl_status.ip_oldest_age << " s)" << END();

        LOG_INFO_I("ue::print_traffic") << " UL HARQ buf: " << ul_status.harq_size << " pkts (oldest ID " << ul_status.harq_oldest_id << ", delay " << ul_status.harq_oldest_age << " s, retry ordinal " << ul_status.harq_oldest_retry_ordinal << ")" << END();
        LOG_INFO_I("ue::print_traffic") << " DL HARQ buf: " << dl_status.harq_size << " pkts (oldest ID " << dl_status.harq_oldest_id << ", delay " << dl_status.harq_oldest_age << " s, retry ordinal " << dl_status.harq_oldest_retry_ordinal << ")" << END();

        LOG_INFO_I("ue::print_traffic") << " UL capture buf: " << ul_status.capture_size << " pkts (oldest UID " << ul_status.capture_oldest_uid << ", delay " << ul_status.capture_oldest_age << " s)" << END();
        LOG_INFO_I("ue::print_traffic") << " DL capture buf: " << dl_status.capture_size << " pkts (oldest UID " << dl_status.capture_oldest_uid << ", delay " << dl_status.capture_oldest_age << " s)" << END();

        LOG_INFO_I("ue::print_traffic") << " UL release buf: " << ul_status.release_size << " pkts (oldest UID " << ul_status.release_oldest_uid << ", delay " << ul_status.release_oldest_age << " s)" << END();
        LOG_INFO_I("ue::print_traffic") << " DL release buf: " << dl_status.release_size << " pkts (oldest UID " << dl_status.release_oldest_uid << ", delay " << dl_status.release_oldest_age << " s)" << END();
    }
}
