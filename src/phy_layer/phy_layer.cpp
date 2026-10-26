/**********************************************
 * Copyright 2022 Nokia
 * Licensed under the BSD 3-Clause Clear License
 * SPDX-License-Identifier: BSD-3-Clause-Clear
 **********************************************/
#include <phy_layer/phy_layer.h>
#include <phy_layer/penetration_model.h>
#include <phy_layer/power_model.h>
#include <utils/rng_seed.h>

double linearToDBm(double linear)
{
    return 10 * log10(1000 * linear);
}

double linearToDb(double linear)
{
    return 10 * log10(linear);
}

double dBmToLinear(double db)
{
    return pow(10, (db - 30) / 10);
}

double dBToLinear(double db)
{
    return pow(10, (db) / 10);
}

void phy_layer::init_metric(int _index)
{
    // Metric variables
    metric_i.index = _index;
}

void phy_layer::init_scenario(int _type, float _eNB_h, float _w, float _antenna_h, float _ue_h, float _ue_speed)
{
    scenario = _type;
    eNB_h = _eNB_h;
    w = _w;
    antenna_h = _antenna_h;
    antenna_h_172 = pow(antenna_h, 1.72);
    ue_h = _ue_h;
    max_speed = _ue_speed * 1000 / 3600;

    check_boundaries();
}

void phy_layer::init_phy(float _freq, float _ue_speed, float _target_ber, float _ue_h, int _tx, float _scaling_factor, int _mimo_l, int _numerology, int _n_sc_rbg, bool _mcs_tables, float _gain_tx, float _gain_rx, float _tx_power, float _alpha_ul, float _nominal_pusch_p0, ul_power_mode _ul_power_mode, float _tx_power_ul, float _power_boost, float _thermal_noise_density, float _NF_UT, float _NF_eNB, int _num_interf_ues, int _num_interf_eNBs, float _d_interference, float _interfered_ratio)
{
    freq = _freq;
    tx_power_eNb = _tx_power;
    alpha_ul = _alpha_ul;
    nominal_pusch_p0 = _nominal_pusch_p0;
    power_mode = _ul_power_mode;
    tx_power_ul = _tx_power_ul;
    doppler_f = freq * _ue_speed / LIGHTSPEED;
    freq_ghz = freq / GHZ2HZ;
    overhead = get_overhead(freq, _tx);
    scaling_factor = _scaling_factor;
    mimo_l = _mimo_l;
    n_sc_rb = _n_sc_rbg;
    numerology = _numerology;
    n_rb_rbg = n_sc_rb / SCH_N_RE_DEFAULT;
    rbg_lookup_index = get_rbg_sndex(n_rb_rbg);
    mcs_tables = _mcs_tables;
    power_boost = _power_boost;
    thermal_noise_density = _thermal_noise_density;
    antenna_gain_tx = _gain_tx;
    antenna_gain_rx = _gain_rx;
    NF_UT = _NF_UT;
    NF_eNB = _NF_eNB;
    num_interf_ues = _num_interf_ues;
    num_interf_eNBs = _num_interf_eNBs;
    d_interference = _d_interference;
    interfered_ratio = _interfered_ratio;
    target_ber=_target_ber;
}

void phy_layer::init_amc(int _modulation_m, int _cqi_m, int _cqi_p)
{
    modulation_m = _modulation_m;
    cqi_mode = _cqi_m;
    cqi_period = _cqi_p;
}

void phy_layer::init_rank(int _period, int _n_antennas,int _mimo_layers)
{
    ri_period = _period;
    max_ri = std::max(
        1,
        std::min(
            std::min(_n_antennas, _mimo_layers),
            NUM_RI_VALUES));
    current_ri = max_ri;
    layers = max_ri;
}

phy_layer::phy_layer(int _tx, int _id, scenario_config _scenario_config, phy_ue_config _phy_ue_config, phy_enb_config _phy_enb_config, bool _stochastics, int _verbosity)
    : fading_gen(phy_rng_seed(
          _stochastics, _id, _tx, PHY_STREAM_FADING)),
      interference_gen(phy_rng_seed(
          _stochastics, _id, _tx, PHY_STREAM_INTERFERENCE)),
      cqi_distance_gen(phy_rng_seed(
          _stochastics, _id, _tx, PHY_STREAM_CQI_DISTANCE)),
      metric_h(
          _phy_enb_config.metric_type,
          _phy_ue_config.beta,
          _phy_ue_config.delay_t,
          _phy_ue_config.delta,
          _phy_enb_config.pf_alpha)

{

    // UE unique id
    id = _id;

    // Transmission direction
    tx = _tx;
    tx_dir = _tx;

    // Enable stochastics
    stochastics = _stochastics;
    // stochastics = false;

    // TX to string
    tx_s = tx == TX_DL ? "dl" : "ul";

    // Verbosity configuration
    verbosity = _verbosity;
    bandwidth = _phy_enb_config.bandwidth;
    throughput_state.set_time_window_ms(
        _phy_enb_config.throughput_time_window_ms);
    throughput_intra_tti_update =
        _phy_enb_config.throughput_intra_tti_update;

    // Metric variables
    init_metric(id);

    // Model variables
    init_scenario(_scenario_config.type, _scenario_config.eNB_h, _scenario_config.w,
                  _scenario_config.antenna_h, _phy_ue_config.ue_h, _phy_ue_config.max_speed);
    penetration_profile = _phy_ue_config.penetration_profile;
    vehicle_profile = _phy_ue_config.vehicle_profile;

    // Phy configuration
    init_phy(_phy_enb_config.frequency, max_speed, _phy_enb_config.target_ber, ue_h,
             tx, _phy_ue_config.scaling_factor, _phy_enb_config.mimo_l, _phy_enb_config.numerology,
             _phy_enb_config.n_sc_rbg, _scenario_config.mcs_tables, tx == TX_DL ? _phy_enb_config.eNB_gain : _phy_enb_config.UT_gain, tx == TX_DL ? _phy_enb_config.UT_gain : _phy_enb_config.eNB_gain, _phy_enb_config.tx_power, _phy_ue_config.alpha_ul, _phy_ue_config.nominal_pusch_p0, _phy_ue_config.power_mode, _phy_ue_config.tx_power_ul, _phy_enb_config.power_boost, _phy_enb_config.thermal_noise, _phy_enb_config.figure_noise_ut, _phy_enb_config.figure_noise_enb, _phy_enb_config.n_int_ues, _phy_enb_config.n_int_eNBs, _phy_enb_config.d_interference, _phy_enb_config.interfered_ratio);

    // AMC
    init_amc(_phy_enb_config.modulation_m, _phy_enb_config.cqi_mode, _phy_ue_config.cqi_period);

    // RI estimation
    init_rank(_phy_ue_config.ri_period, _phy_ue_config.n_antennas,_phy_enb_config.mimo_l);

    // Update Rates
    init_update_rates(doppler_f, cqi_period, ri_period);

    if (mcs_tables)
    {
        const int layer_index = get_mcs_layer_index(layers);
        std::copy(
            MCS_SINR[modulation_m][rbg_lookup_index][layer_index],
            MCS_SINR[modulation_m][rbg_lookup_index][layer_index]
                + MCS_INDEX_MAX,
            mcs_lookup);
    }
}

void phy_layer::compute_coherence_bw()
{
    switch (scenario)
    {
    case RURAL_MACROCELL:
        ds = pow(10, -7.46);
        break;
    case URBAN_MICROCELL:
        ds = pow(10, (-7.14 - 6.83) / 2.0);
        break;
    case URBAN_MACROCELL:
        ds = pow(10, (-6.955 - 6.28) / 2.0);
        break;
    case INDOOR_OPEN_OFFICE:
        ds = pow(10, (-7.692 - 7.173) / 2.0);
        break;
    case INDOOR_MIXED_OFFICE:
        ds = pow(10, (-7.692 - 7.173) / 2.0);
        break;
    case INDOOR_SHOPPING_MALL:
        ds = pow(10, (-9.35 - 9.44) / 2.0);
        break;
    }
    bc = 1 / (BC_CORRELATION_DEFAULT * ds);

    float n = (float)bandwidth / bc;
   // float n = 10000;

    if (n_rbs >= 2 * n)
    {
        sinr_rbs = ceil(n);
        sinr_step = floor(n_rbs / n);
    }
    else
    {
        sinr_rbs = n_rbs;
        sinr_step = 1;
    }
}

void phy_layer::check_boundaries()
{
    antenna_h = std::max(5.0f, antenna_h);
    antenna_h = std::min(antenna_h, 50.0f);

    w = std::max(5.0f, w);
    w = std::min(w, 50.0f);

    switch (scenario)
    {
    case RURAL_MACROCELL:
        eNB_h = std::max(10.0f, eNB_h);
        eNB_h = std::min(eNB_h, 150.0f);

        ue_h = std::max(1.0f, ue_h);
        ue_h = std::min(ue_h, 10.0f);

        break;
    case URBAN_MICROCELL:
        eNB_h = 10.0f;

        ue_h = std::max(1.5f, ue_h);
        ue_h = std::min(ue_h, 22.5f);

        break;
    case URBAN_MACROCELL:
        eNB_h = 25.0f;

        ue_h = std::max(1.5f, ue_h);
        ue_h = std::min(ue_h, 22.5f);
        break;
    }
}
void phy_layer::init(
    int _n_rbs,
    int _bandwidth,
    int _carrier_prbs)
{
    n_rbs = _n_rbs;
    n_tot_RB =
        _carrier_prbs > 0
            ? _carrier_prbs
            : n_rbs * n_rb_rbg;
    // bandwidth = _bandwidth;
    cqi_s = 0;
    mcs_s = 0;
    eff_s = 0;
    db_sinr_s = 0;
    l_sinr_s = 0;
    cqi_v = std::vector<int>(n_rbs, 0);
    mcs_v = std::vector<int>(n_rbs, 0);
    eff_v = std::vector<float>(n_rbs, 0);
    rsrp_v = std::vector<float>(n_rbs, 0);
    db_sinr_v = std::vector<float>(n_rbs, 0);
    metric_v = std::vector<float>(n_rbs, 0);
    tp_v = std::vector<float>(n_rbs, 0);
    l_sinr_v = std::vector<float>(n_rbs, 0);
    is_init = true;
    compute_coherence_bw();
    // Noise and interference
    estimate_noise_interference(tx_power_eNb,
                                tx == TX_DL ? num_interf_eNBs : num_interf_ues, d_interference, interfered_ratio,
                                tx == TX_DL ? NF_UT : NF_eNB,
                                antenna_gain_tx, antenna_gain_rx);
}

float phy_layer::compute_rayleigh()
{
    const float in_phase = sinr_stochastics(fading_gen);
    const float quadrature = sinr_stochastics(fading_gen);
    return rayleigh_power_gain_db(in_phase, quadrature);
}

float phy_layer::compute_pathloss_ABG(float distance, bool los) 
{
    float alpha, beta, gamma;

    switch (scenario)
    {
    case RURAL_MACROCELL:

        if (los)
        {
            alpha = 2.16;
            beta = 32.4;
            gamma = 2;
        }
        else
        {
            alpha = 2.75;
            beta = 32.4;
            gamma = 2;
        }

        break;
    case URBAN_MACROCELL:

        if (los)
        {
            if (freq_ghz < 6)
            {
                alpha = 2.2;
                beta = 28;
                gamma = 2;
            }
            else
            {
                alpha = 1.9;
                beta = 35.8;
                gamma = 1.9;
            }
        }
        else
        {
            alpha = 3.5;
            beta = 13.6;
            gamma = 2.4;
        }
        break;

    case URBAN_MICROCELL:

        if (los)
        {
            if (freq_ghz < 6)
            {
                alpha = 2.27;
                beta = 27.02;
                gamma = 2;
            }
            else
            {
                alpha = 1.1;
                beta = 46.8;
                gamma = 2.1;
            }
        }
        else
        {
            alpha = 2.8;
            beta = 31.4;
            gamma = 2.7;
        }

        break;
    case INDOOR_OPEN_OFFICE:
    case INDOOR_MIXED_OFFICE:
        if (los)
        {
            if (freq_ghz < 6)
            {
                alpha = 1.87;
                beta = 32.82;
                gamma = 2;
            }
            else
            {
                alpha = 1.6;
                beta = 32.9;
                gamma = 1.8;
            }
        }
        else
        {
            if (freq_ghz < 6)
            {
                alpha = 4.33;
                beta = 11.5;
                gamma = 2;
            }
            else
            {
                alpha = 3.9;
                beta = 19;
                gamma = 2.1;
            }
        }
        break;
    case INDOOR_SHOPPING_MALL:

        if (los)
        {
            alpha = 1.9;
            beta = 31.2;
            gamma = 2.2;
        }
        else
        {
            alpha = 2;
            beta = 34.4;
            gamma = 2.3;
        }
        break;
    }

    const float effective_distance = std::max(distance - d_in, 1.0f);
    float plb =
        10 * alpha * log10(effective_distance)
        + beta
        + 10 * gamma * log10(freq_ghz);

    return plb;
}
float phy_layer::compute_penetration_losses(
    float building_sample,
    float vehicle_sample)
{
    if (o2i == OUTDOOR)
        return pltw = 0.0f;

    if (o2i == IN_CAR)
        return pltw = vehicle_penetration_loss_db(
                   vehicle_profile, vehicle_sample);

    if (o2i != IN_BUILDING || penetration_profile == PENETRATION_NONE)
        return pltw = 0.0f;

    if (penetration_profile == PENETRATION_LEGACY_AUTO)
    {
        if (freq_ghz <= 6.0f)
            return pltw = 12.0f;
        penetration_profile =
            scenario == RURAL_MACROCELL
                ? PENETRATION_LOW_LOSS
                : PENETRATION_HIGH_LOSS;
    }

    pltw = building_penetration_loss_db(
        penetration_profile,
        freq_ghz,
        building_sample);
    return pltw;
}

float phy_layer::calculateOxygenLoss()
{
    float a_oxygen = 0;

    switch (static_cast<int>(freq_ghz))
    {
    case 53:
        a_oxygen = 1;
        break;
    case 54:
        a_oxygen = 2.2;
        break;
    case 55:
        a_oxygen = 4;
        break;
    case 56:
        a_oxygen = 6.6;
        break;
    case 57:
        a_oxygen = 9.7;
        break;
    case 58:
        a_oxygen = 12.6;
        break;
    case 59:
        a_oxygen = 14.6;
        break;
    case 60:
        a_oxygen = 15;
        break;
    case 61:
        a_oxygen = 14.6;
        break;
    case 62:
        a_oxygen = 14.3;
        break;
    case 63:
        a_oxygen = 10.5;
        break;
    case 64:
        a_oxygen = 6.8;
        break;
    case 65:
        a_oxygen = 3.9;
        break;
    case 66:
        a_oxygen = 1.9;
        break;
    case 67:
        a_oxygen = 1;
        break;
    default:
        a_oxygen = 0;
        break;
    }

    return a_oxygen;
}
float phy_layer::compute_losses(float distance)
{

    a_oxygen = calculateOxygenLoss();

    float pltotal = a_oxygen * distance / 1000 + pltw + plin;
    return pltotal;
}

int phy_layer::get_ri()
{
    return current_ri;
}

int phy_layer::get_cqi(int f_index)
{
    if (cqi_mode == CQI_WIDEBAND)
        return cqi_s;
    if (cqi_mode == CQI_RBGS)
        return cqi_v[f_index];
    return -1;
}
float phy_layer::get_eff(int f_index)
{
    if (cqi_mode == CQI_WIDEBAND)
        return eff_s;
    if (cqi_mode == CQI_RBGS)
        return eff_v[f_index];
    return -1;
}

int phy_layer::get_mcs(int f_index)
{
    if (cqi_mode == CQI_WIDEBAND)
        return mcs_s;
    if (cqi_mode == CQI_RBGS)
        return mcs_v[f_index];
    return -1;
}

float phy_layer::get_sinr(int f_index)
{
    if (cqi_mode == CQI_WIDEBAND)
        return db_sinr_s;
    if (cqi_mode == CQI_RBGS)
        return db_sinr_v[f_index];
    return -1;
}

float phy_layer::get_linear_sinr(int f_index)
{
    if (cqi_mode == CQI_WIDEBAND)
        return l_sinr_s;
    if (cqi_mode == CQI_RBGS)
        return l_sinr_v[f_index];
    return -1;
}

float phy_layer::get_mean_sinr()
{
    return db_sinr_s;
}

float phy_layer::get_mean_rsrp()
{
    return rsrp_s;
}

float phy_layer::get_mean_cqi()
{
    return (float)cqi_s;
}

float phy_layer::get_mean_mcs()
{
    return (float)mcs_s;
}

float phy_layer::get_mean_eff()
{
    return eff_s;
}

float phy_layer::get_mean_scheduler_metric() const
{
    if (metric_v.empty())
        return 0.0f;
    float total = 0.0f;
    for (float metric : metric_v)
        total += metric;
    return total / static_cast<float>(metric_v.size());
}

int phy_layer::get_modulation_index()
{
    return modulation_m;
}

int phy_layer::get_n_layers()
{
    return layers;
}

int phy_layer::estimate_cqi_from_distance(float distance)
{
    return (int)std::min(
        15.0,
        std::max(
            1.0,
            pow(
                int(15 * ((ENB_MAX_RANGE - distance) / ENB_MAX_RANGE)),
                0.5)
                + distance_cqi_dist(cqi_distance_gen)));
}

float phy_layer::estimate_eff_from_sinr(float sinr)
{
    return log2(1 + sinr / (-log(5 * target_ber / 1.5)));
}

float phy_layer::estimate_cqi_from_se(float se)
{
    int i = 0;
    for (i = 0; i < NUM_CQI_VALUES && EFF_2_CQI[modulation_m][i + 1] <= se; i++)
        ;
    return i;
}

float phy_layer::estimate_mcs_from_se(float se, float &eff)
{
    int i = 0;
    for (i = 0; i < NUM_MCS_VALUES - 1 && EFF_2_MCS[modulation_m][i + 1] <= se; i++)
        ;
    eff = EFF_2_MCS[modulation_m][i];
    return i;
}

float phy_layer::estimate_eff_from_mcs(int index)
{
    return EFF_2_MCS[modulation_m][index];
}

int phy_layer::get_mcs_index(float sinr)
{
    int i = 0;
    if (mcs_lookup[0] > sinr)
        return -1;
    for (i = 0; i < MCS_INDEX_MAX - 1 && mcs_lookup[i + 1] <= sinr; i++)
        ;
    return i;
}

void phy_layer::channel_q_tables(int f)
{

    mcs_v[f] = get_mcs_index(db_sinr_v[f]);
    if (mcs_v[f] >= 0)
    {
        eff_v[f] = estimate_eff_from_mcs(mcs_v[f]);
        cqi_v[f] = estimate_cqi_from_se(eff_v[f]);
    }
    else
    {
        eff_v[f] = 0;
        cqi_v[f] = 0;
    }
}

void phy_layer::channel_q_efficiency(int f)
{
    float efficiency = estimate_eff_from_sinr(l_sinr_v[f]);
    cqi_v[f] = estimate_cqi_from_se(efficiency);
    if (cqi_v[f] > 0)
        mcs_v[f] = estimate_mcs_from_se(efficiency, eff_v[f]);
    else
    {
        mcs_v[f] = 0;
        eff_v[f] = 0;
    }
}

void phy_layer::estimate_channel_q(int f)
{
    if (mcs_tables)
        channel_q_tables(f);
    else
        channel_q_efficiency(f);
    mcs_s += mcs_v[f];
    cqi_s += cqi_v[f];
    eff_s += eff_v[f];
}

void phy_layer::estimate_ri()
{
    if (tx == TX_DL)
    {
        current_ri = estimate_rank_from_mean_sinr(
            modulation_m, max_ri, db_sinr_s);
    }
    else
    {
        current_ri = 1;
    }
}
void phy_layer::estimate_noise_interference(float _tx_power, int _n_ues, float _d_interference, float _interfered_ratio, float _figure, float _gain_tx, float _gain_rx)
{
    float tx_power;

    if (tx == TX_UL)
    {
        // Until explicit neighbor allocations exist, UL interference is represented as
        // an aggregate per-PRB power spectral density.
        tx_power =
            -23 + uniform_stochastics(interference_gen) * 46;
    }
    else
    {
        tx_power = downlink_power_per_prb_dbm(_tx_power, n_tot_RB);
    }

    noise_figure = _figure;
    antenna_gain_tx = _gain_tx;
    antenna_gain_rx = _gain_rx;

    const float noise =
        thermal_noise_per_prb_dbm(thermal_noise_density, noise_figure, numerology);
    linear_noise = dBmToLinear(noise);

    const float n_interference = _n_ues;
    const float random = uniform_stochastics(interference_gen);
    d_interference = _d_interference + random * 100;

    float linear_interference = 0.0f;
    linear_interference_transmitted = 0.0f;
    if (n_interference > 0.0f && _interfered_ratio > 0.0f)
    {
        const float power_dbm =
            tx_power + 10.0f * log10(_interfered_ratio);
        linear_interference_transmitted =
            n_interference * dBmToLinear(power_dbm);
        const float interference =
            linearToDBm(linear_interference_transmitted)
            - compute_losses(d_interference)
            - compute_pathloss_ABG(d_interference, true);
        linear_interference = dBmToLinear(interference);
    }

    noise_interf = linearToDBm(linear_noise + linear_interference);
}

float phy_layer::ul_power_per_prb_dbm(float distance, int allocated_prbs)
{
    const float nominal_power_per_prb =
        nominal_pusch_p0 + alpha_ul * compute_pathloss_ABG(distance, true);
    return calculate_ul_power_allocation(
               power_mode,
               tx_power_ul,
               nominal_power_per_prb,
               allocated_prbs)
        .per_prb_dbm;
}

float phy_layer::sinr_power_model(float _tx_power, float distance, float pathloss, float _macro_fading)
{
    const float tx_power_per_prb =
        tx_dir == TX_UL
            ? ul_power_per_prb_dbm(distance, ul_scheduling_prbs)
            : downlink_power_per_prb_dbm(_tx_power, n_tot_RB);
    current_tx_power_per_prb_dbm = tx_power_per_prb;
    float rayleigh = stochastics ? compute_rayleigh() : 0;

    float sinr = tx_power_per_prb + rayleigh - pathloss + _macro_fading
                 + antenna_gain_tx + antenna_gain_rx - noise_interf
                 - 10 * log10(current_ri);
    return sinr + sinr_offset_db;
}

void phy_layer::estimate_sinr(float distance, int f, float _macro_fading)
{
    // SINR
    pathloss = compute_losses(distance);

    if (f % sinr_step == 0)
    {

        db_sinr_v[f] = sinr_power_model(tx_power_eNb, distance, pathloss, _macro_fading);
    }
    else
    {
        db_sinr_v[f] = db_sinr_v[f - 1];
    }

    db_sinr_s += db_sinr_v[f];
    // db_sinr_v is referenced to one PRB. Convert the desired received PRB power
    // to one resource element for the RSRP approximation.
    rsrp_v[f] =
        db_sinr_v[f] + noise_interf
        - 10.0f * log10(static_cast<float>(SCH_N_RE_DEFAULT))
        + power_boost;
    rsrp_s += rsrp_v[f];
    l_sinr_v[f] = pow(10, db_sinr_v[f] / 10.0);
}

void phy_layer::average_sinr()
{
    db_sinr_s /= n_rbs;
    rsrp_s /= n_rbs;
    l_sinr_s = pow(10, db_sinr_s / 10.0);
}

void phy_layer::average_cqi()
{
    cqi_s /= n_rbs;
    mcs_s /= n_rbs;
    eff_s /= n_rbs;
}

void phy_layer::reset_cqi()
{
    cqi_s = 0;
    mcs_s = 0;
    eff_s = 0;
}

void phy_layer::set_logger(log_handler *_logger)
{
    phy_log = true;
    logger = _logger;
}

void phy_layer::prepare_metrics(float oldest_t, float avg_tp)
{
    // Get UE info for metrics
    metric_i.req_time = oldest_t;
    metric_i.avrg_tp =
        metric_h.uses_throughput_history()
            ? throughput_metric_average()
            : avg_tp * MBIT2BIT / S2MS;
    metric_i.current_delay = current_t - metric_i.req_time;
}

float phy_layer::standalone_rate_bits_per_tti() const
{
    float bits_per_symbol = 0.0f;
    for (float rate : tp_v)
        bits_per_symbol += rate;
    return bits_per_symbol
           * SCH_N_OFDM_DEFAULT
           * static_cast<float>(1 << numerology);
}

float phy_layer::throughput_metric_average() const
{
    if (!throughput_intra_tti_update || !throughput_state.initialized())
        return throughput_state.average_throughput();
    return throughput_state.projected_average(provisional_service_bits);
}

void phy_layer::prepare_scheduler_tti(bool active)
{
    provisional_service_bits = 0.0f;
    if (!metric_h.uses_throughput_history() || !active)
        return;
    throughput_state.prepare(standalone_rate_bits_per_tti());
}

void phy_layer::record_provisional_service(float scheduled_bits)
{
    if (!metric_h.supports_provisional_history()
        || !throughput_intra_tti_update)
        return;
    provisional_service_bits += std::max(scheduled_bits, 0.0f);
}

void phy_layer::update_scheduler_state(float effective_bits, bool active)
{
    if (!metric_h.uses_throughput_history() || !active)
        return;
    prepare_scheduler_tti(true);
    throughput_state.update(effective_bits, true);
}

void phy_layer::reset_scheduler_state()
{
    throughput_state.reset();
    provisional_service_bits = 0.0f;
}

void phy_layer::estimate_metric(int f)
{
    metric_i.current_tp = tp_v[f];

    if (!metric_h.is_rr())
        metric_v[f] = metric_h.get_metric(metric_i, current_t, f);
}

void phy_layer::estimate_tp(int f)
{
    float current_tp = current_ri * n_sc_rb * eff_v[f] * (1 - overhead) * scaling_factor;
    tp_v[f] = current_tp;
}

float phy_layer::get_metric(int f, int n_enabled, int rr_rank, float priority)
{
    if (metric_h.is_rr())
        return metric_h.get_rr_metric(f, rr_rank, n_enabled);
    if (metric_h.is_throughput_metric())
    {
        metric_info current = metric_i;
        current.current_tp = tp_v[f];
        if (metric_h.uses_throughput_history())
            current.avrg_tp = throughput_metric_average();
        return metric_h.get_metric(current, current_t, f) * priority;
    }
    return metric_v[f] * priority;
}

float phy_layer::get_tp(int f)
{
    return tp_v[f];
}

void phy_layer::finalize_ul_allocation(int allocated_prbs)
{
    if (tx_dir != TX_UL || !is_init)
        return;

    const int prbs = normalized_prb_count(allocated_prbs);
    const float finalized_power_per_prb =
        ul_power_per_prb_dbm(last_distance, prbs);
    const float power_delta_db =
        finalized_power_per_prb - current_tx_power_per_prb_dbm;
    ul_scheduling_prbs = prbs;

    if (std::fabs(power_delta_db) < 1e-6f)
        return;

    current_tx_power_per_prb_dbm = finalized_power_per_prb;
    reset_cqi();
    for (int f = 0; f < n_rbs; f++)
    {
        db_sinr_v[f] += power_delta_db;
        l_sinr_v[f] = pow(10, db_sinr_v[f] / 10.0);
        rsrp_v[f] += power_delta_db;
        estimate_channel_q(f);
        estimate_tp(f);
        estimate_metric(f);
    }
    average_cqi();
    db_sinr_s += power_delta_db;
    l_sinr_s = pow(10, db_sinr_s / 10.0);
    rsrp_s += power_delta_db;
}

void phy_layer::init_update_rates(float _doppler_f, int _cqi_p, int _ri_p)
{
    if (_doppler_f > 0)
    {
        tc = 0.423 / _doppler_f;
        float min_sinr_update = tc / 0.001;
        int min_update = std::min(_cqi_p, _ri_p);
        if (min_sinr_update > min_update)
            sinr_period = min_update;
        else
            sinr_period = std::max(1, (int)floor(min_sinr_update));
    }
    else
    {
        tc = -1;
        sinr_period = std::min(_cqi_p, _ri_p);
    }
}

void phy_layer::estimate_channel_state(float distance, phy_shared &phy_s, float _macro_fading, int _o2i, int _tx_dir, const pos2d &pos, float oldest_t, float avg_tp, float _current_t, float _sinr_offset_db)
{
    // doppler_f = freq * _speed / LIGHTSPEED;

    current_t = _current_t;
    last_distance = distance;
    o2i = _o2i;
    tx_dir = _tx_dir;
    macro_fading = _macro_fading;
    sinr_offset_db = _sinr_offset_db;

    if (!d_in_computed)
    {
        d_in =
            o2i == IN_BUILDING
                    && penetration_profile != PENETRATION_NONE
                ? phy_s.get_indoor_depth(distance)
                : 0.0f;
        pltw = compute_penetration_losses(
            phy_s.get_building_penetration_sample(),
            phy_s.get_vehicle_penetration_sample());
        plin =
            o2i == IN_BUILDING
                ? 0.5f * d_in
                : 0.0f;

        d_in_computed = true;
    }

    bool dumb = period_counter % 1000 == 0;
    bool update_cqi = period_counter % cqi_period == 0;
    bool update_ri = period_counter % ri_period == 0;
    bool update_sinr = period_counter % sinr_period == 0;
    if (update_cqi)
        reset_cqi();
    if (update_sinr)
    {
        // estimate_attenuation(distance,pos,phy_s);
        // pathloss=compute_pathloss(distance,_los);
        rsrp_s = 0;
        db_sinr_s = 0;
    }
    prepare_metrics(oldest_t, avg_tp);

    for (int i = 0; i < n_rbs; i++)
    {
        if (update_sinr)

            estimate_sinr(distance, i, macro_fading);
        if (update_cqi)
        {
            estimate_channel_q(i);
            estimate_tp(i);
        }
        if (update_cqi || metric_h.is_throughput_metric())
        {
            estimate_metric(i);
            /*if(tx==TX_DL && dumb)
            {
                std::cout << "MCS: ";
                for(const auto& value: mcs_v) std::cout << value << " : ";
                std::cout << std::endl;
                std::cout << "TP: ";
                for(const auto& value: tp_v) std::cout << value << " : ";
                std::cout << std::endl;
                std::cout << "M: ";
                for(const auto& value: metric_v) std::cout << value << " : ";
                std::cout << std::endl;
            }*/
        }
    }

    if (update_cqi)
        average_cqi();
    if (update_sinr)
        average_sinr();
    if (update_ri)
        estimate_ri();
    if (phy_log)
    {

        logger->log_partial("sinr:{} rsrp:{} cqi:{} mcs:{} eff:{} ri:{} tx:{} ts:{}", db_sinr_s, rsrp_s, cqi_s, mcs_s, eff_s, current_ri, tx, current_t);
        logger->flush();
    }
    period_counter++;
}
