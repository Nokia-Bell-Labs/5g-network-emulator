/**********************************************
 * Copyright 2022 Nokia
 * Licensed under the BSD 3-Clause Clear License
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 **********************************************/

#include <mac_layer/resource_grid.h>
#include <stdexcept>
#include <vector>

// Checks if a valid duplexing type (TDD or FDD) was selected
int grid::check_duplexing_type(int d_type)
{
    if (d_type == TDD || d_type == FDD)
        return d_type;
    return DUPLEX_DEFAULT;
}

// Checks if a valid configuration type (0 or 1) was selected
int grid::check_sch_config(int config)
{
    if (config != SCH_CONFIG_0 && config != SCH_CONFIG_1)
        return 1;
    else
    {
        for (int i = 0; i < 4; i++)
        {
            if (SCH_RBG_SIZE[config][i][0] <= n_freq_rb && SCH_RBG_SIZE[config][i][1] > n_freq_rb)
                return SCH_RBG_SIZE[config][i][2];
        }
    }
    return 1;
}

// Checks if a valid scheduling mode (Localized / Distributed) was selected
int grid::check_sch_mode(int mode)
{
    if (mode != SCH_LOCALIZED_MODE && mode != SCH_DISTRIBUTED_MODE)
    {
        LOG_ERROR_I("grid::check_sch_mode") << " Wrong schedulling type [" << mode << "], setting Localized mode as default" << END();
        return SCH_LOCALIZED_MODE;
    }
    return mode;
}

// Checks if a valid numerology values (0 to 5) was selected
int grid::check_numerology(int n)
{
    if (n > SCH_MAX_NUMEROLOGY)
    {
        LOG_ERROR_I("grid::check_numerology") << " Wrong numerology number [" << n << "], rolling back to default numerology: " << SCH_NUMEROLOGY_DEFAULT << END();
        return SCH_NUMEROLOGY_DEFAULT;
    }
    if (n < 0)
    {
        LOG_ERROR_I("grid::check_numerology") << " Wrong numerology number, rolling back to default numerology: " << SCH_NUMEROLOGY_DEFAULT << END();
        return SCH_NUMEROLOGY_DEFAULT;
    }
    return n;
}

// Checks if any valid configuration is possible with the available bandwidth.
// Throwing lets main report a stable EX_SOFTWARE-style status and still run destructors;
// exit(-1) bypassed both and surfaced as the opaque status 255 on Unix.
void grid::check_frequency_division(int n)
{
    if (n < SCH_N_RB_MIN)
    {
        throw std::runtime_error("not enough bandwidth for selected numerology");
    }
}

// Plot the built grid info
void grid::plot_info()
{
    LOG_INFO_I("grid::plot_info") << " Created Reseource Grid with: " << END();
    LOG_INFO_I("grid::plot_info") << " Numerology: " << numerology << END();
    LOG_INFO_I("grid::plot_info") << " Max. bandwidth: " << bandwidth << END();
    LOG_INFO_I("grid::plot_info") << " Number of available RBs: " << n_rb_total << END();
    LOG_INFO_I("grid::plot_info") << " Number of available REs: " << n_re_total << END();
    LOG_INFO_I("grid::plot_info") << " Max. possible throughput (no MIMO): " << n_rb_total * n_ofdm_symbols * n_sc_rb * EFF_2_CQI[1][15] * BIT2MBIT * S2MS << END();
    LOG_INFO_I("grid::plot_info") << " Number of RBs in frequency: " << n_freq_rb << END();
    LOG_INFO_I("grid::plot_info") << " Number of RBGs in frequency: " << n_freq_rbg << END();
    LOG_INFO_I("grid::plot_info") << " Number of RBs in time: " << n_time_rb << END();
    LOG_INFO_I("grid::plot_info") << " Number of RBGs in time: " << n_time_rbg << END();
    if (log)
    {
        if (duplexing_t == TDD)
            logger->log_force("tx:{} n:{} bw:{} rb:{} re:{} tp:{} f:{} t:{} mt:{} tdd:{}/{}/{} ", tx, numerology, bandwidth, n_rb_total, n_re_total, n_rb_total * n_ofdm_symbols * n_sc_rb * EFF_2_CQI[1][15] * BIT2MBIT * S2MS, n_freq_rbg, n_time_rbg, metric_t, tdd_h.get_dl_slots(), tdd_h.get_ul_slots(), tdd_h.get_transition());
        else
            logger->log_force("tx:{} n:{} bw:{} rb:{} re:{} tp:{} f:{} t:{} mt:{} tdd:false", tx, numerology, bandwidth, n_rb_total, n_re_total, n_rb_total * n_ofdm_symbols * n_sc_rb * EFF_2_CQI[1][15] * BIT2MBIT * S2MS, n_freq_rbg, n_time_rbg, metric_t);
    }
}
// MAC LOGGER
//  Returns the number of resource blocks in the frequency axis
int grid::get_n_freq_rb() { return n_freq_rb; }
int grid::get_n_freq_rbg() { return n_freq_rbg; }
int grid::get_n_sc_rbg() { return n_sc_rbg; }
int grid::get_rbg_size() { return n_freq_rb_rbg; }
int grid::get_logical_units() { return n_freq_rb_rbg; }

// Returns the number of resource blocks in the time axis
int grid::get_n_time_rb() { return n_time_rb; }
void grid::add_current_ts(float t) { current_t = t; }

// Initializes the resource grid
void grid::init(std::vector<ue> *ue_list)
{
    this->ue_list = ue_list;
    for (int t = 0; t < n_time_rbg; t++)
    {
        std::vector<rb> v_f;
        for (int f = 0; f < n_freq_rbg; f++)
        {
            if (log)
                v_f.push_back(rb(t, f, tx, mimo_layers, n_sc_rbg, ue_list, verbosity, logger));
            else
                v_f.push_back(rb(t, f, tx, mimo_layers, n_sc_rbg, ue_list, verbosity));
        }

        rb_grid.push_back(v_f);
    }
    plot_info();
}

// New step with a duration of a Transmission Time Interval (TTI - 1ms)
void grid::step()
{
    int syms = 0;
    int n_enabled = 0;
    last_step_metrics = grid_step_metrics();
    if (ue_list != nullptr)
    {
        for (std::vector<ue>::iterator it = ue_list->begin(); it != ue_list->end(); ++it)
        {
            if (it->is_enabled())
                n_enabled++;
            if (it->is_enabled() && it->has_packets(tx))
                last_step_metrics.active_ues_with_data++;
            bool scheduler_active =
                it->is_enabled() && it->has_packets(tx);
            if (scheduler_active)
            {
                scheduler_active = false;
                for (int f = 0; f < n_freq_rbg; f++)
                {
                    if (it->get_tp(tx, f) > 0.0f)
                    {
                        scheduler_active = true;
                        break;
                    }
                }
            }
            it->prepare_scheduler_tti(tx, scheduler_active);
        }
    }
    std::vector<bool> scheduled_ues(ue_list != nullptr ? ue_list->size() : 0, false);
    for (int t = 0; t < n_time_rbg; t++)
    {
        if (duplexing_t == TDD)
            syms = tdd_h.get_syms();
        else
            syms = n_sym_rbg;
        if (syms > 0)
        {
            last_step_metrics.available_rbg_count += n_freq_rbg;
            last_step_metrics.grid_capacity_bits +=
                n_freq_rbg
                * n_sc_rbg
                * syms
                * EFF_2_CQI[1][15];
        }

        auto record_rbg = [&](int f)
        {
            if (rb_grid[t][f].was_scheduled())
            {
                last_step_metrics.scheduled_rbg_count++;
                last_step_metrics.scheduled_bits += rb_grid[t][f].last_scheduled_bits();
                last_step_metrics.effective_bits += rb_grid[t][f].last_effective_bits();
                if (rb_grid[t][f].last_effective_bits() > 0.0f)
                    last_step_metrics.effective_rbg_count++;
                int ue_index = rb_grid[t][f].last_ue_index();
                if (ue_index >= 0 && ue_index < (int)scheduled_ues.size() && !scheduled_ues[ue_index])
                {
                    scheduled_ues[ue_index] = true;
                    last_step_metrics.scheduled_ue_count++;
                }
            }
        };

        if (tx == TX_UL)
        {
            // UL power is a per-UE total-power budget. Plan the complete frequency
            // allocation for this time group before deriving per-PRB power and MCS.
            for (int f = 0; f < n_freq_rbg; f++)
            {
                rb_grid[t][f].estimate_params(syms, current_t, n_enabled);
                const int ue_index = rb_grid[t][f].planned_ue_index();
                if (ue_index >= 0)
                    (*ue_list)[ue_index].record_provisional_scheduler_service(
                        tx, rb_grid[t][f].planned_scheduled_bits(syms));
            }

            std::vector<int> allocated_prbs(
                ue_list != nullptr ? ue_list->size() : 0, 0);
            for (int f = 0; f < n_freq_rbg; f++)
            {
                const int ue_index = rb_grid[t][f].planned_ue_index();
                if (ue_index >= 0 && ue_index < (int)allocated_prbs.size())
                    allocated_prbs[ue_index] += n_freq_rb_rbg;
            }
            for (size_t ue_index = 0; ue_index < allocated_prbs.size(); ue_index++)
                (*ue_list)[ue_index].finalize_ul_allocation(allocated_prbs[ue_index]);

            for (int f = 0; f < n_freq_rbg; f++)
            {
                rb_grid[t][f].handle_packet(syms);
                record_rbg(f);
            }
        }
        else
        {
            for (int f = 0; f < n_freq_rbg; f++)
            {
                rb_grid[t][f].estimate_params(syms, current_t, n_enabled);
                const int ue_index = rb_grid[t][f].planned_ue_index();
                if (ue_index >= 0)
                    (*ue_list)[ue_index].record_provisional_scheduler_service(
                        tx, rb_grid[t][f].planned_scheduled_bits(syms));
                rb_grid[t][f].handle_packet(syms);
                record_rbg(f);
            }
        }
    }
    const int total_rbg_count = n_freq_rbg * n_time_rbg;
    last_step_metrics.structural_unavailable_rbg_count =
        total_rbg_count - last_step_metrics.available_rbg_count;
    last_step_metrics.empty_rbg_count =
        last_step_metrics.available_rbg_count
        - last_step_metrics.scheduled_rbg_count;
    last_step_metrics.zero_effective_rbg_count =
        last_step_metrics.scheduled_rbg_count
        - last_step_metrics.effective_rbg_count;
    if (last_step_metrics.available_rbg_count > 0)
        last_step_metrics.utilization_ratio =
            (float)last_step_metrics.scheduled_rbg_count
            / (float)last_step_metrics.available_rbg_count;
    if (last_step_metrics.scheduled_bits > 0.0f)
        last_step_metrics.scheduling_efficiency_ratio = last_step_metrics.effective_bits / last_step_metrics.scheduled_bits;
    if (log)
    {
        logger->log_force(
            "summary:1 tx:{} available:{} assigned:{} effective:{} "
            "wasted:{} empty:{} structural:{} capacity:{} "
            "utilization:{} ts:{} \n",
            tx,
            last_step_metrics.available_rbg_count,
            last_step_metrics.scheduled_rbg_count,
            last_step_metrics.effective_rbg_count,
            last_step_metrics.zero_effective_rbg_count,
            last_step_metrics.empty_rbg_count,
            last_step_metrics.structural_unavailable_rbg_count,
            last_step_metrics.grid_capacity_bits,
            last_step_metrics.utilization_ratio,
            current_t);
    }
}

float grid::assignRFBandwidth(float bandwidth)
{
    float RF_bandwidth;

    if (bandwidth == 1.4e6)
    {
        RF_bandwidth = 1.1e6;
    }
    else if (bandwidth == 3e6)
    {
        RF_bandwidth = 2.7e6;
    }
    else if (bandwidth == 5e6)
    {
        RF_bandwidth = 4.5e6;
    }
    else if (bandwidth == 10e6)
    {
        RF_bandwidth = 9e6;
    }
    else if (bandwidth == 15e6)
    {
        RF_bandwidth = 13.5e6;
    }
    else if (bandwidth == 20e6)
    {
        RF_bandwidth = 18e6;
    }
    else if (bandwidth == 40e6)
    {
        RF_bandwidth = 38.16e6;
    }
    else if (bandwidth == 50e6)
    {
        RF_bandwidth = 47.88e6;
    }
    else if (bandwidth == 60e6)
    {
        RF_bandwidth = 58.32e6;
    }
    else if (bandwidth == 80e6)
    {
        RF_bandwidth = 78.13e6;
    }
    else if (bandwidth == 100e6)
    {
        RF_bandwidth = 98.28e6;
    }
    else
    {
        RF_bandwidth = bandwidth * 0.9;
    }

    return RF_bandwidth;
}

// Constructor of the resource grid class. It handles all the configuration data and builds the resource grid
grid::grid(int _tx, int _mimo_layers, int _numerology, int _n_re_f, int _n_re_t, int _metric_t, int _bandwidth, int _scheduling_mode,
           int _scheduling_type, int _scheduling_config,
           int _duplexing_type, float _ratio_DL_UL, tdd_config _tdd_c, int _verbosity)
    : tdd_h(_tdd_c, _tx)
{
    verbosity = _verbosity;
    tx = _tx;
    mimo_layers = _mimo_layers;
    numerology = check_numerology(_numerology);
    bandwidth = assignRFBandwidth(_bandwidth);
    duplexing_t = check_duplexing_type(_duplexing_type);
    if (duplexing_t == FDD)
    {
        if (tx == 0)
        {
            bandwidth *= _ratio_DL_UL;
        }
        else if (tx == 1)
        {
            bandwidth *= (1.0 - _ratio_DL_UL);
        }
    }

    n_sc_rb = _n_re_f;
    // Max allowed number of resource block given the numerology
    int n_rb_max = SCH_N_RB_MAX[numerology];
    sc_spacing = SCH_SC_SPACING[numerology] * KHZ2HZ;
    n_freq_rb = std::min((int)floor(bandwidth / (sc_spacing * n_sc_rb)), n_rb_max);
    check_frequency_division(n_freq_rb);
    n_ofdm_symbols = _n_re_t;
    scheduling_mode = check_sch_mode(_scheduling_mode);
    // Division in time domain
    scheduling_type = _scheduling_type;
    scheduling_config = _scheduling_config;
    if (scheduling_mode == SCH_DISTRIBUTED_MODE)
    {
        n_time_rb = pow(2, numerology);
        n_time_rbg = n_time_rb;
        if (scheduling_type == 0)
        {
            n_freq_rb_rbg = check_sch_config(scheduling_config);
        }
        else
            n_freq_rb_rbg = 1;
        n_freq_rbg = (int)(n_freq_rb / n_freq_rb_rbg);//aqui deberia ser ceil chema
        n_rb_total = n_freq_rb * n_time_rb;
        n_re_total = n_ofdm_symbols * n_sc_rb * n_freq_rb_rbg;
        n_sc_rbg = n_sc_rb * n_freq_rb_rbg;
        n_sym_rbg = (int(n_ofdm_symbols * (n_time_rb / n_time_rbg)));
    }
    else
    {
        n_time_rb = pow(2, numerology);
        n_time_rbg = 1;
        if (scheduling_type == 0)
        {
            n_freq_rb_rbg = check_sch_config(scheduling_config);
        }
        else
            n_freq_rb_rbg = 1;
        n_freq_rbg = (int)(n_freq_rb / n_freq_rb_rbg);
        n_rb_total = n_freq_rb * n_time_rb;
        n_re_total = n_ofdm_symbols * n_sc_rb * n_freq_rb_rbg * n_time_rb;
        n_sc_rbg = n_sc_rb * n_freq_rb_rbg;
        n_sym_rbg = (int(n_ofdm_symbols * (n_time_rb / n_time_rbg)));
    }
    n_logical_units = n_time_rb / n_time_rbg;
    metric_t = _metric_t;
    tdd_h.set_syms(n_sym_rbg);
}
