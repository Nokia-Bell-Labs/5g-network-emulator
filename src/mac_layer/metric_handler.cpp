/**********************************************
* Copyright 2022 Nokia
* Licensed under the BSD 3-Clause Clear License
* SPDX-License-Identifier: BSD-3-Clause-Clear
**********************************************/

#include <mac_layer/metric_handler.h>

// MAX METRIC HANDLER METHODS
//-------------------------
void max_metric_handler::reset()
{
    assigned = false; 
    tp = 0.0; 
    value = -1; 
    index = -1; 
    id = -1; 
}

void max_metric_handler::assign(float _tp, float _value, int _index, int _id)
{
    assigned = true; 
    tp = _tp; 
    value = _value; 
    index = _index; 
    id = _id; 
}

bool max_metric_handler::evaluate(float metric)
{
    return metric >= value;
}

bool max_metric_handler::is_assigned() const
{
    return assigned; 
}

bool max_metric_handler::has_data() const
{
    return tp > 0; 
}

float max_metric_handler::get_tp() const
{
    return tp; 
}

int max_metric_handler::get_index() const
{
    return index; 
}

int max_metric_handler::get_id() const
{
    return id; 
}

float max_metric_handler::get_value() const
{
    return value; 
}

// METRIC HANDLER METHODS
//-------------------------

metric_handler::metric_handler(
    int _metric_type,
    float _bet_beta,
    float _delay_t,
    float _delta,
    float _pf_alpha)
{
    metric_t = check_metric(_metric_type);
    (void)_bet_beta;
    assign_throughput_recipe(_pf_alpha);
    assign_metric();
    delay_t = _delay_t; 
    delta = _delta; 
}

float metric_handler::get_metric(metric_info metric_i, float current_t, int f)
{
    return (this->*metric_f_ptr)(metric_i, current_t, f);
}

void metric_handler::assign_metric()
{
    if(metric_t == METRIC_FIFO) metric_f_ptr = &metric_handler::fifo; 
    if(metric_t == METRIC_BET) metric_f_ptr = &metric_handler::throughput;
    if(metric_t == METRIC_DIST_DELAY) metric_f_ptr = &metric_handler::dist_delay; 
    if(metric_t == METRIC_W_DELAY) metric_f_ptr = &metric_handler::w_delay; 
    if(metric_t == METRIC_MAX_TP) metric_f_ptr = &metric_handler::throughput; 
    if(metric_t == METRIC_RR) metric_f_ptr = &metric_handler::rr; 
    if(metric_t == METRIC_PF) metric_f_ptr = &metric_handler::throughput; 
}

void metric_handler::assign_throughput_recipe(float pf_alpha)
{
    if (metric_t == METRIC_MAX_TP)
        throughput_recipe = {1.0f, 0.0f, false, false};
    else if (metric_t == METRIC_PF)
        throughput_recipe = {pf_alpha, 1.0f, true, true};
    else if (metric_t == METRIC_BET)
        throughput_recipe = {0.0f, 1.0f, true, true};
}

float metric_handler::throughput(
    metric_info metric_i,
    float current_t,
    int f)
{
    (void)current_t;
    (void)f;
    const float rate = std::max(metric_i.current_tp, 0.0f);
    const float numerator =
        throughput_recipe.rate_exponent == 0.0f
            ? 1.0f
            : std::pow(rate, throughput_recipe.rate_exponent);
    const float denominator =
        throughput_recipe.uses_history
            ? std::pow(
                  std::max(metric_i.avrg_tp, 1e-6f),
                  throughput_recipe.history_exponent)
            : 1.0f;
    return numerator / denominator;
}

float metric_handler::fifo(metric_info metric_i, float current_t, int f)
{
    return current_t - metric_i.req_time;
}

float metric_handler::dist_delay(metric_info metric_i, float current_t, int f)
{
    return 1/(metric_i.delay_t - metric_i.current_delay);
}

float metric_handler::w_delay(metric_info metric_i, float current_t, int f)
{
    return -(log(metric_i.delta)/metric_i.delay_t) * metric_i.current_delay;
}

float metric_handler::rr(metric_info metric_i, float current_t, int f)
{
    return 0.0;
}

bool metric_handler::is_rr()
{
    return metric_t == METRIC_RR; 
}

bool metric_handler::is_pf() const
{
    return metric_t == METRIC_PF;
}

bool metric_handler::is_throughput_metric() const
{
    return metric_t == METRIC_BET
           || metric_t == METRIC_MAX_TP
           || metric_t == METRIC_PF;
}

bool metric_handler::uses_throughput_history() const
{
    return throughput_recipe.uses_history;
}

bool metric_handler::supports_provisional_history() const
{
    return throughput_recipe.supports_provisional_history;
}

const throughput_metric_recipe &metric_handler::get_throughput_recipe() const
{
    return throughput_recipe;
}

float metric_handler::get_rr_metric(int f, int rank, int n_enabled)
{
    if(prev_f != f)
    {
        rr_index++;
        if(rr_index >= n_enabled) rr_index = 0; 
        prev_f = f; 
    }
    if(rr_index == rank) return 1.0;
    else return 0.0;
}

int metric_handler::check_metric(int metric)
{
    if (!(metric == METRIC_FIFO ||
            metric == METRIC_BET ||
            metric == METRIC_DIST_DELAY ||
            metric == METRIC_W_DELAY ||
            metric == METRIC_MAX_TP ||
            metric == METRIC_RR ||
            metric == METRIC_PF))
            {
                return SCH_METRIC_DEFAULT; 
            }
    else return metric;
}
