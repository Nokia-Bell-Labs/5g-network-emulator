/**********************************************
* Copyright 2022 Nokia
* Licensed under the BSD 3-Clause Clear License
* SPDX-License-Identifier: BSD-3-Clause-Clear
**********************************************/

#include <mac_layer/metric_handler.h>

#include <mutex>
#include <stdexcept>
#include <utility>

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

// BUILT-IN METRICS
//-------------------------
// Each expression is kept as it was when the metrics were member functions, operation by
// operation, so that scores do not change by a single bit.

namespace
{
float throughput_score(
    const throughput_metric_recipe &recipe,
    const metric_info &metric_i)
{
    const float rate = std::max(metric_i.current_tp, 0.0f);
    const float numerator =
        recipe.rate_exponent == 0.0f
            ? 1.0f
            : std::pow(rate, recipe.rate_exponent);
    const float denominator =
        recipe.uses_history
            ? std::pow(
                  std::max(metric_i.avrg_tp, 1e-6f),
                  recipe.history_exponent)
            : 1.0f;
    return numerator / denominator;
}

metric_score_fn throughput_score_fn(const throughput_metric_recipe &recipe)
{
    return [recipe](const metric_info &metric_i, double, int)
    {
        return throughput_score(recipe, metric_i);
    };
}

float fifo_score(const metric_info &metric_i, double current_t, int)
{
    return static_cast<float>(current_t - metric_i.req_time);
}

float dist_delay_score(const metric_info &metric_i, double, int)
{
    return static_cast<float>(
        1.0 / (metric_i.delay_t - metric_i.current_delay));
}

float w_delay_score(const metric_info &metric_i, double, int)
{
    return static_cast<float>(
        -(log(metric_i.delta) / metric_i.delay_t) * metric_i.current_delay);
}

float rr_score(const metric_info &, double, int)
{
    return 0.0;
}

std::vector<metric_entry> builtin_metrics()
{
    std::vector<metric_entry> entries;

    metric_entry fifo;
    fifo.id = METRIC_FIFO;
    fifo.name = "fifo";
    fifo.score = fifo_score;
    entries.push_back(fifo);

    // BET: no rate term, inverse service history.
    metric_entry bet;
    bet.id = METRIC_BET;
    bet.name = "bet";
    bet.recipe = {0.0f, 1.0f, true, true};
    bet.score = throughput_score_fn(bet.recipe);
    bet.throughput_based = true;
    entries.push_back(bet);

    metric_entry dist_delay;
    dist_delay.id = METRIC_DIST_DELAY;
    dist_delay.name = "dist_delay";
    dist_delay.score = dist_delay_score;
    entries.push_back(dist_delay);

    metric_entry w_delay;
    w_delay.id = METRIC_W_DELAY;
    w_delay.name = "w_delay";
    w_delay.score = w_delay_score;
    entries.push_back(w_delay);

    // Maximum throughput: the current rate, no history.
    metric_entry max_tp;
    max_tp.id = METRIC_MAX_TP;
    max_tp.name = "max_tp";
    max_tp.recipe = {1.0f, 0.0f, false, false};
    max_tp.score = throughput_score_fn(max_tp.recipe);
    max_tp.throughput_based = true;
    entries.push_back(max_tp);

    metric_entry rr;
    rr.id = METRIC_RR;
    rr.name = "rr";
    rr.score = rr_score;
    rr.round_robin = true;
    entries.push_back(rr);

    // PF: the exponent of the rate is configured (pf_alpha), so the recipe and the score
    // are built per handler. Without a configuration it is the classic r / T.
    metric_entry pf;
    pf.id = METRIC_PF;
    pf.name = "pf";
    pf.recipe = {1.0f, 1.0f, true, true};
    pf.score = throughput_score_fn(pf.recipe);
    pf.throughput_based = true;
    pf.bind = [](const metric_params &params)
    {
        const throughput_metric_recipe recipe =
            {params.pf_alpha, 1.0f, true, true};
        return bound_metric{throughput_score_fn(recipe), recipe};
    };
    entries.push_back(pf);

    return entries;
}

// The only way into the list, for the built-ins and for everything else.
void insert_metric(std::vector<metric_entry> &entries, metric_entry entry)
{
    if (!entry.score && !entry.bind)
        throw std::invalid_argument(
            "metric '" + entry.name + "' (id " + std::to_string(entry.id)
            + ") has no score function");
    for (const metric_entry &known : entries)
    {
        if (known.id == entry.id)
            throw std::invalid_argument(
                "metric id " + std::to_string(entry.id) + " is already registered as '"
                + known.name + "', cannot register '" + entry.name + "'");
    }
    entries.push_back(std::move(entry));
}

struct registry_state
{
    std::mutex lock;
    std::vector<metric_entry> entries;

    registry_state()
    {
        for (metric_entry &entry : builtin_metrics())
            insert_metric(entries, std::move(entry));
    }
};

// Built on first use, with the built-ins already in it, wherever that use comes from.
registry_state &state()
{
    static registry_state registry;
    return registry;
}
} // namespace

// METRIC REGISTRY METHODS
//-------------------------

void metric_registry::register_metric(metric_entry entry)
{
    registry_state &registry = state();
    std::lock_guard<std::mutex> guard(registry.lock);
    insert_metric(registry.entries, std::move(entry));
}

std::optional<metric_entry> metric_registry::find(int id)
{
    registry_state &registry = state();
    std::lock_guard<std::mutex> guard(registry.lock);
    for (const metric_entry &entry : registry.entries)
    {
        if (entry.id == id)
            return entry;
    }
    return std::nullopt;
}

std::vector<int> metric_registry::ids()
{
    registry_state &registry = state();
    std::lock_guard<std::mutex> guard(registry.lock);
    std::vector<int> result;
    for (const metric_entry &entry : registry.entries)
        result.push_back(entry.id);
    return result;
}

metric_registrar::metric_registrar(metric_entry entry)
{
    metric_registry::register_metric(std::move(entry));
}

// METRIC HANDLER METHODS
//-------------------------

metric_handler::metric_handler(
    int _metric_type,
    float _bet_beta,
    double _delay_t,
    float _delta,
    float _pf_alpha)
{
    const metric_entry entry = check_metric(_metric_type);
    metric_t = entry.id;
    round_robin = entry.round_robin;

    metric_params params;
    params.bet_beta = _bet_beta;
    params.delay_t = _delay_t;
    params.delta = _delta;
    params.pf_alpha = _pf_alpha;
    if (entry.bind)
    {
        bound_metric bound = entry.bind(params);
        score_fn = std::move(bound.score);
        throughput_recipe = bound.recipe;
    }
    else
    {
        score_fn = entry.score;
        throughput_recipe = entry.recipe;
    }
    throughput_based =
        entry.throughput_based || throughput_recipe.uses_history;
    delay_t = _delay_t;
    delta = _delta;
}

float metric_handler::get_metric(const metric_info &metric_i, double current_t, int f)
{
    return score_fn(metric_i, current_t, f);
}

bool metric_handler::is_rr()
{
    return round_robin;
}

bool metric_handler::is_pf() const
{
    return metric_t == METRIC_PF;
}

bool metric_handler::is_throughput_metric() const
{
    return throughput_based;
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

metric_entry metric_handler::check_metric(int metric)
{
    std::optional<metric_entry> entry = metric_registry::find(metric);
    if (!entry)
    {
        std::string known;
        for (int id : metric_registry::ids())
            known += (known.empty() ? "" : ", ") + std::to_string(id);
        throw std::invalid_argument(
            "metric_type " + std::to_string(metric)
            + " is not registered (registered: " + known + ")");
    }
    return *entry;
}
