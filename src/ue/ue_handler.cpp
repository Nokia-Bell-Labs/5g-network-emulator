#include <functional>
#include <algorithm>
#include <cctype>
#include <stdexcept>

#include <simulator/configuration_loader.h>
#include <ue/ue_handler.h>
#include <utils/terminal_logging.h>

ue_handler::ue_handler(int _threads)
{
    threads = _threads;
    multithreading = (threads > 0);
    if(multithreading) tp.init(threads);
    n_ues = 0;
}

void ue_handler::add_ues(std::chrono::microseconds * _init_t, ue_full_config ue_c, phy_enb_config _phy_enb_config, scenario_config _scenario_c, pdcp_config _pdcp_config_ul, pdcp_config _pdcp_config_dl, harq_config _harq_config, bool _stochastics)
{
    if (!ue_c.id.empty())
    {
        const bool numeric = std::all_of(ue_c.id.begin(), ue_c.id.end(),
            [](unsigned char c) { return std::isdigit(c) != 0; });
        const bool valid = std::all_of(ue_c.id.begin(), ue_c.id.end(),
            [](unsigned char c) {
                return std::isalnum(c) != 0 || c == '_' || c == '-' || c == '.';
            });
        if (numeric || !valid)
            throw std::invalid_argument(
                "textual ue_id must contain a non-digit and use [A-Za-z0-9_.-]");
    }

    n_ues += ue_c.n_ues;

    int init_ids = id;
    for(; id < init_ids + ue_c.n_ues; id++)
    {
        ue_config instance = ue_c.ue_c;
        if (!ue_c.id.empty())
        {
            const int local = id - init_ids;
            instance.control_id = ue_c.n_ues == 1
                ? ue_c.id : ue_c.id + "_" + std::to_string(local);
        }
        ue_list.emplace_back(id, instance, _scenario_c, _phy_enb_config,
                             _pdcp_config_ul, _pdcp_config_dl, _harq_config,
                             ue_c.ue_type, _init_t, _stochastics);
    }

    log_ue_creation(id, ue_c.ue_type, ue_c.ue_c.ue_m.n_antennas, ue_c.ue_c.ue_m.cqi_period, ue_c.ue_c.ue_m.ri_period, ue_c.ue_c.ue_m.scaling_factor);
}

void ue_handler::init()
{
    for(std::vector<ue>::iterator it = ue_list.begin(); it != ue_list.end(); it++)
    {
        if(multithreading) tp.do_job(std::bind(&ue::step, it));
        it->init();
    }
    // The jobs above bind iterators into ue_list, so from here on the container must not
    // grow or be reordered. Everything that follows relies on that invariant.
    refresh_enabled_ranks(ue_list);
}

void ue_handler::refresh_enabled_ranks(std::vector<ue> &list)
{
    int rank = 0;
    for(size_t i = 0; i < list.size(); i++)
    {
        if(list[i].is_enabled()) list[i].overrides().rr_rank = rank++;
        else list[i].overrides().rr_rank = -1;
    }
}

void ue_handler::add_ue(std::chrono::microseconds * _init_t, ue_full_config ue_c, phy_enb_config _phy_enb_config, scenario_config _scenario_c, pdcp_config _pdcp_config_ul, pdcp_config _pdcp_config_dl, harq_config _harq_config, bool _stochastics)
{
    n_ues++;
    id++;
    ue_list.emplace_back(id, ue_c.ue_c, _scenario_c, _phy_enb_config, _pdcp_config_ul, _pdcp_config_dl, _harq_config, ue_c.ue_type, _init_t, _stochastics);

    log_ue_creation(id, ue_c.ue_type, ue_c.ue_c.ue_m.n_antennas,
                    ue_c.ue_c.ue_m.cqi_period, ue_c.ue_c.ue_m.ri_period, ue_c.ue_c.ue_m.scaling_factor);
    tp.do_job(std::bind(&ue::step, &ue_list.back()));
    ue_list.back().init();
}

void ue_handler::step(double _current_t)
{
    for(size_t i = 0; i < ue_list.size(); i++)
    {
        ue_list[i].add_current_t(_current_t);
    }

    if(multithreading)
    {
        tp.do_jobs();
        tp.wait_threads();
    }
    else
    {
        for(size_t i = 0; i < ue_list.size(); i++)
        {
            ue_list[i].step();
        }
    }
}

int ue_handler::enabled_count() const
{
    int n = 0;
    for(size_t i = 0; i < ue_list.size(); i++)
        if(ue_list[i].is_enabled()) n++;
    return n;
}

void ue_handler::print_traffic()
{
    float total_tp_ul = 0;
    float total_tp_dl = 0;
    float total_l_ul = 0;
    float total_l_dl = 0;
    float total_verb_ue = 0;
    for(size_t i = 0; i < ue_list.size(); i++)
    {
        if(!ue_list[i].is_enabled()) continue;
        ue_list[i].print_traffic();
        if(ue_list[i].get_traffic_verbosity())
        {
            total_tp_dl += ue_list[i].get_avg_tp(TX_DL);
            total_tp_ul += ue_list[i].get_avg_tp(TX_UL);
            total_l_dl += ue_list[i].get_avg_l(TX_DL);
            total_l_ul += ue_list[i].get_avg_l(TX_UL);
            total_verb_ue += 1;
        }
    }
    if(total_verb_ue > 0)
    {
        LOG_INFO_I("ue_handler::print_traffic") << " Total DL throughput transmitted: " << total_tp_dl << " Mbps - with a mean value per user of: " << total_tp_dl/total_verb_ue << " Mbps." << END();
        LOG_INFO_I("ue_handler::print_traffic") << " Total UL throughput transmitted: " << total_tp_ul << " Mbps - with a mean value per user of: " << total_tp_ul/total_verb_ue << " Mbps." << END();
        LOG_INFO_I("ue_handler::print_traffic") << " Mean DL latency per user of: " << total_l_dl/total_verb_ue << " s." << END();
        LOG_INFO_I("ue_handler::print_traffic") << " Mean UL latency per user of: " << total_l_ul/total_verb_ue << " s." << END();
    }
}

std::vector<ue>* ue_handler::get_ue_list()
{
    return &ue_list;
}

void ue_handler::log_ue_creation(int _n_ues, int ue_type, int _n_antennas, int _cqi_period, int _ri_period, float scaling_factor)
{
    LOG_INFO_I("ue_handler::log_ue_creation") << " Created " << _n_ues << " with the following configuration: " << END();
    LOG_INFO_I("ue_handler::log_ue_creation") << " Type;  " << ((ue_type == SIM_UE)? "SIM_UE" : "REAL_UE") << " with the following configuration: " << END();
    LOG_INFO_I("ue_handler::log_ue_creation") << " Number of antennas: " << _n_antennas << END();
    LOG_INFO_I("ue_handler::log_ue_creation") << " Scaling factor: " << scaling_factor << END();
    LOG_INFO_I("ue_handler::log_ue_creation") << " CQI update period: " << _cqi_period << END();
    LOG_INFO_I("ue_handler::log_ue_creation") << " RI period: " << _ri_period << END();
}
