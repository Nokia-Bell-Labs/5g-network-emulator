/**********************************************
* Copyright 2022 Nokia
* Licensed under the BSD 3-Clause Clear License
* SPDX-License-Identifier: BSD-3-Clause-Clear
**********************************************/

#include <mac_layer/mac_layer.h>
#include <chrono>
#include <utils/monitoring/monitoring_manager.h>

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

std::string make_text_log_line(const std::string &direction, const std::string &line)
{
    const auto now_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    std::string lp = "emulator_log";
    lp += ",component=mac";
    lp += ",dir=" + escape_influx_tag(direction);
    lp += " message=\"" + escape_influx_string_field(line) + "\"";
    lp += " " + std::to_string(now_ns);
    return lp;
}
}


mac_layer::mac_layer(bool _threading, std::vector<ue> *ue_list, mac_config mac_c, tdd_config _tdd_c, log_config log_c, int _verbosity)
        : grid_dl(T_DL, mac_c.mimo_layers, mac_c.numerology, mac_c.n_re_freq, mac_c.n_ofdm_syms, mac_c.metric_type,
                    mac_c.bandwidth, mac_c.scheduling_mode, mac_c.scheduling_type,
                    mac_c.scheduling_config, mac_c.duplexing_type,mac_c.ratio_DL_UL, _tdd_c),
            grid_ul(T_UL, mac_c.mimo_layers, mac_c.numerology, mac_c.n_re_freq, mac_c.n_ofdm_syms, mac_c.metric_type, 
                    mac_c.bandwidth, mac_c.scheduling_mode, mac_c.scheduling_type,
                    mac_c.scheduling_config, mac_c.duplexing_type, mac_c.ratio_DL_UL,_tdd_c)
            {
                ue_list_v = ue_list;
                threading = _threading;
                if(threading) 
                {
                    tp.init(8);
                    tp.do_job(std::bind (&grid::step, &grid_dl));
                    tp.do_job(std::bind (&grid::step, &grid_ul));
                } 

                verbosity = _verbosity; 
                log = log_c.log_mac;
                if(log)
                {
                    logger_dl.init("logs/" + log_c.log_id + "/mac/", "grid_log_dl", log_c.log_freq);
                    logger_dl.set_monitoring_callback([](const std::string &line){
                        monitoring_manager::instance().send_text_line(make_text_log_line("dl", line));
                    });
                    grid_dl.set_logger(&logger_dl);
                    logger_ul.init("logs/" + log_c.log_id + "/mac/", "grid_log_ul", log_c.log_freq);
                    logger_ul.set_monitoring_callback([](const std::string &line){
                        monitoring_manager::instance().send_text_line(make_text_log_line("ul", line));
                    });
                    grid_ul.set_logger(&logger_ul);
                }
                bandwidth = mac_c.bandwidth; 
                grid_dl.init(ue_list); 
                grid_ul.init(ue_list); 
            }

void mac_layer::plot_info()
{
    grid_dl.plot_info();
    grid_ul.plot_info();
}

void mac_layer::plot_info(int tx)
{
    if(tx == T_DL) grid_dl.plot_info();
    if(tx == T_UL) grid_ul.plot_info();
}

int mac_layer::get_bandwidth(){ return bandwidth; }
int mac_layer::get_n_freq_rb() { return grid_dl.get_n_freq_rb(); }
int mac_layer::get_n_freq_rbg() { return grid_dl.get_n_freq_rbg(); }
int mac_layer::get_n_sc_rbg() { return grid_dl.get_n_sc_rbg(); }
int mac_layer::get_rbg_size() { return grid_dl.get_rbg_size(); }
int mac_layer::get_logical_units() { return grid_dl.get_logical_units(); }
int mac_layer::get_n_time_rb() { return grid_dl.get_n_time_rb(); }

void mac_layer::init(std::vector<ue> *ue_list)
{
    grid_dl.init(ue_list);
    grid_ul.init(ue_list);
}

void mac_layer::flush_logs()
{
    if(log)
    {
        logger_dl.flush(); 
        logger_ul.flush(); 
    }
}

void mac_layer::update_ts(float t)
{
    grid_dl.add_current_ts(t);
    grid_ul.add_current_ts(t);
}

void mac_layer::step(float current_t)
{  

    update_ts(current_t);    
    if(threading)
    {
        tp.do_jobs();
        tp.wait_threads();
    }
    else
    {
        grid_dl.step();
        grid_ul.step();
    }

    if (ue_list_v != nullptr)
    {
        for (ue &terminal : *ue_list_v)
            terminal.commit_scheduler_tti();
    }

    monitoring_manager &monitoring = monitoring_manager::instance();
    if(monitoring.is_enabled() && monitoring.get_config().emit_mac_scheduler)
    {
        const grid_step_metrics &dl_metrics = grid_dl.get_last_step_metrics();
        metric_point dl_point;
        dl_point.measurement = "mac_scheduler";
        dl_point.tags["tx_dir"] = "dl";
        dl_point.ts_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        dl_point.fields["available_rbg_sum"] = make_metric_field(dl_metrics.available_rbg_count, field_aggregation::sum);
        dl_point.fields["structural_unavailable_rbg_sum"] = make_metric_field(dl_metrics.structural_unavailable_rbg_count, field_aggregation::sum);
        dl_point.fields["scheduled_rbg_sum"] = make_metric_field(dl_metrics.scheduled_rbg_count, field_aggregation::sum);
        dl_point.fields["effective_rbg_sum"] = make_metric_field(dl_metrics.effective_rbg_count, field_aggregation::sum);
        dl_point.fields["zero_effective_rbg_sum"] = make_metric_field(dl_metrics.zero_effective_rbg_count, field_aggregation::sum);
        dl_point.fields["empty_rbg_sum"] = make_metric_field(dl_metrics.empty_rbg_count, field_aggregation::sum);
        dl_point.fields["scheduled_ues_sum"] = make_metric_field(dl_metrics.scheduled_ue_count, field_aggregation::sum);
        dl_point.fields["active_ues_with_data_mean"] = make_metric_field(dl_metrics.active_ues_with_data, field_aggregation::mean);
        dl_point.fields["scheduled_bits_sum"] = make_metric_field(dl_metrics.scheduled_bits, field_aggregation::sum);
        dl_point.fields["effective_bits_sum"] = make_metric_field(dl_metrics.effective_bits, field_aggregation::sum);
        dl_point.fields["grid_capacity_bits_last"] = make_metric_field(dl_metrics.grid_capacity_bits, field_aggregation::last);
        dl_point.fields["utilization_ratio_mean"] = make_metric_field(dl_metrics.utilization_ratio, field_aggregation::mean);
        dl_point.fields["scheduling_efficiency_ratio_mean"] = make_metric_field(dl_metrics.scheduling_efficiency_ratio, field_aggregation::mean);
        monitoring.publish(dl_point);

        const grid_step_metrics &ul_metrics = grid_ul.get_last_step_metrics();
        metric_point ul_point;
        ul_point.measurement = "mac_scheduler";
        ul_point.tags["tx_dir"] = "ul";
        ul_point.ts_ns = dl_point.ts_ns;
        ul_point.fields["available_rbg_sum"] = make_metric_field(ul_metrics.available_rbg_count, field_aggregation::sum);
        ul_point.fields["structural_unavailable_rbg_sum"] = make_metric_field(ul_metrics.structural_unavailable_rbg_count, field_aggregation::sum);
        ul_point.fields["scheduled_rbg_sum"] = make_metric_field(ul_metrics.scheduled_rbg_count, field_aggregation::sum);
        ul_point.fields["effective_rbg_sum"] = make_metric_field(ul_metrics.effective_rbg_count, field_aggregation::sum);
        ul_point.fields["zero_effective_rbg_sum"] = make_metric_field(ul_metrics.zero_effective_rbg_count, field_aggregation::sum);
        ul_point.fields["empty_rbg_sum"] = make_metric_field(ul_metrics.empty_rbg_count, field_aggregation::sum);
        ul_point.fields["scheduled_ues_sum"] = make_metric_field(ul_metrics.scheduled_ue_count, field_aggregation::sum);
        ul_point.fields["active_ues_with_data_mean"] = make_metric_field(ul_metrics.active_ues_with_data, field_aggregation::mean);
        ul_point.fields["scheduled_bits_sum"] = make_metric_field(ul_metrics.scheduled_bits, field_aggregation::sum);
        ul_point.fields["effective_bits_sum"] = make_metric_field(ul_metrics.effective_bits, field_aggregation::sum);
        ul_point.fields["grid_capacity_bits_last"] = make_metric_field(ul_metrics.grid_capacity_bits, field_aggregation::last);
        ul_point.fields["utilization_ratio_mean"] = make_metric_field(ul_metrics.utilization_ratio, field_aggregation::mean);
        ul_point.fields["scheduling_efficiency_ratio_mean"] = make_metric_field(ul_metrics.scheduling_efficiency_ratio, field_aggregation::mean);
        monitoring.publish(ul_point);
    }
    flush_logs(); 
}
