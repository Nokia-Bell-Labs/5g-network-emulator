// Metric registry: the built-in metrics register through the same function as any other,
// their scores do not change by a bit, and an unknown identifier is an error.
// Same style as pf_metric_test.cpp: <cassert> and nothing else.
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

#include <mac_layer/metric_handler.h>
#include <phy_layer/phy_config.h>
#include <simulator/configuration_loader.h>
#include <ue/ue.h>

namespace
{
// The scores as they were written before the metrics became registry entries. They are
// kept here, apart from the implementation, so that the comparison is not a metric
// against itself.
float reference_throughput(
    const throughput_metric_recipe &recipe, const metric_info &info)
{
    const float rate = std::max(info.current_tp, 0.0f);
    const float numerator =
        recipe.rate_exponent == 0.0f
            ? 1.0f
            : std::pow(rate, recipe.rate_exponent);
    const float denominator =
        recipe.uses_history
            ? std::pow(
                  std::max(info.avrg_tp, 1e-6f),
                  recipe.history_exponent)
            : 1.0f;
    return numerator / denominator;
}

throughput_metric_recipe reference_recipe(int id, float pf_alpha)
{
    if (id == METRIC_MAX_TP)
        return {1.0f, 0.0f, false, false};
    if (id == METRIC_PF)
        return {pf_alpha, 1.0f, true, true};
    if (id == METRIC_BET)
        return {0.0f, 1.0f, true, true};
    return {};
}

float reference_score(
    int id, float pf_alpha, const metric_info &info, double current_t)
{
    if (id == METRIC_FIFO)
        return static_cast<float>(current_t - info.req_time);
    if (id == METRIC_DIST_DELAY)
        return static_cast<float>(
            1.0 / (info.delay_t - info.current_delay));
    if (id == METRIC_W_DELAY)
        return static_cast<float>(
            -(log(info.delta) / info.delay_t) * info.current_delay);
    if (id == METRIC_RR)
        return 0.0;
    return reference_throughput(reference_recipe(id, pf_alpha), info);
}

// Bit pattern, so that a sign of zero or a NaN payload cannot hide a difference.
bool same_bits(float a, float b)
{
    std::uint32_t x;
    std::uint32_t y;
    std::memcpy(&x, &a, sizeof x);
    std::memcpy(&y, &b, sizeof y);
    return x == y;
}

bool same_recipe(
    const throughput_metric_recipe &a, const throughput_metric_recipe &b)
{
    return same_bits(a.rate_exponent, b.rate_exponent)
           && same_bits(a.history_exponent, b.history_exponent)
           && a.uses_history == b.uses_history
           && a.supports_provisional_history == b.supports_provisional_history;
}

bool throws_invalid_argument(int id)
{
    try
    {
        metric_handler handler(id);
    }
    catch (const std::invalid_argument &)
    {
        return true;
    }
    return false;
}

void builtins_keep_scores_and_recipes()
{
    const std::vector<float> rates =
        {-5.0f, 0.0f, 1e-3f, 4.0f, 20.0f, 1.0e6f, 3.3e9f};
    const std::vector<float> averages =
        {0.0f, 1e-7f, 1e-6f, 0.5f, 100.0f, 5.0e7f};
    const std::vector<double> req_times = {0.0, 0.123, 5.5};
    const std::vector<double> times = {0.0, 0.1, 7.25, 43200.0001};
    const std::vector<double> delays = {0.0, 0.01, 0.0999, 0.1, 0.2, 3.0};
    const std::vector<double> delay_ts = {0.1, 0.5};
    const std::vector<float> deltas = {0.001f, 0.8f, 1.0f};
    const std::vector<float> alphas = {0.0f, 0.5f, 1.0f, 2.0f, 7.0f};

    const int ids[] = {
        METRIC_FIFO, METRIC_BET, METRIC_DIST_DELAY, METRIC_W_DELAY,
        METRIC_MAX_TP, METRIC_RR, METRIC_PF};

    for (int id : ids)
    {
        for (float alpha : alphas)
        {
            metric_handler handler(id, 0.5f, 0.1, 0.001f, alpha);
            assert(same_recipe(
                handler.get_throughput_recipe(),
                reference_recipe(id, alpha)));
            assert(handler.is_rr() == (id == METRIC_RR));
            assert(handler.is_pf() == (id == METRIC_PF));
            assert(
                handler.is_throughput_metric()
                == (id == METRIC_BET || id == METRIC_MAX_TP
                    || id == METRIC_PF));
            assert(
                handler.uses_throughput_history()
                == (id == METRIC_BET || id == METRIC_PF));
            assert(
                handler.supports_provisional_history()
                == (id == METRIC_BET || id == METRIC_PF));

            for (float rate : rates)
            for (float average : averages)
            for (double req_time : req_times)
            for (double t : times)
            for (double delay : delays)
            for (double delay_t : delay_ts)
            for (float delta : deltas)
            {
                metric_info info;
                info.current_tp = rate;
                info.avrg_tp = average;
                info.req_time = req_time;
                info.current_delay = delay;
                info.delay_t = delay_t;
                info.delta = delta;
                assert(same_bits(
                    handler.get_metric(info, t, 3),
                    reference_score(id, alpha, info, t)));
            }
        }
    }
}

void builtin_ids_are_registered_first()
{
    const std::vector<int> ids = metric_registry::ids();
    assert(ids.size() >= 7);
    for (int id = 0; id < 7; id++)
        assert(ids[id] == id);
    for (int id = 0; id < 7; id++)
        assert(metric_registry::find(id).has_value());
    assert(metric_registry::find(METRIC_PF)->name == "pf");
    assert(metric_registry::find(METRIC_RR)->round_robin);
}

// Remembers what it was handed, to show where each field comes from.
metric_info last_seen;
int calls = 0;

metric_entry make_test_entry()
{
    metric_entry entry;
    entry.id = 1000;
    entry.name = "test_weighted";
    entry.recipe = {1.0f, 0.5f, true, false};
    entry.score = [](const metric_info &info, double t, int f)
    {
        last_seen = info;
        calls++;
        return info.current_tp * 2.0f + static_cast<float>(t) + f;
    };
    return entry;
}

void registered_metric_is_selected()
{
    metric_registry::register_metric(make_test_entry());
    assert(metric_registry::find(1000).has_value());

    metric_handler handler(1000, 0.5f, 0.1, 0.001f, 3.0f);
    metric_info info;
    info.current_tp = 10.0f;
    info.ue_id = 42;
    info.tx_dir = TX_UL;
    calls = 0;
    assert(same_bits(handler.get_metric(info, 0.5, 2), 22.5f));
    assert(calls == 1);
    assert(last_seen.ue_id == 42);
    assert(last_seen.tx_dir == TX_UL);

    assert(same_recipe(
        handler.get_throughput_recipe(), {1.0f, 0.5f, true, false}));
    assert(handler.uses_throughput_history());
    assert(!handler.supports_provisional_history());
    // A metric that uses history is evaluated on every TTI, like PF.
    assert(handler.is_throughput_metric());
    assert(!handler.is_rr());
    assert(!handler.is_pf());

    // The built-ins are not affected by what has been added.
    metric_handler pf(METRIC_PF, 0.5f, 0.1, 0.001f, 1.0f);
    metric_info cold;
    cold.current_tp = 20.0f;
    cold.avrg_tp = 100.0f;
    assert(same_bits(pf.get_metric(cold, 0.0, 0), 20.0f / 100.0f));
}

void configured_and_round_robin_entries()
{
    // A recipe and a score that depend on the configuration, built per handler.
    metric_entry bound;
    bound.id = 1001;
    bound.name = "test_bound";
    bound.throughput_based = true;
    bound.bind = [](const metric_params &params)
    {
        const float alpha = params.pf_alpha;
        return bound_metric{
            [alpha](const metric_info &info, double, int)
            {
                return info.current_tp * alpha;
            },
            throughput_metric_recipe{alpha, 0.0f, false, false}};
    };
    metric_registry::register_metric(bound);

    metric_handler handler(1001, 0.5f, 0.1, 0.001f, 4.0f);
    metric_info info;
    info.current_tp = 3.0f;
    assert(same_bits(handler.get_metric(info, 0.0, 0), 12.0f));
    assert(same_bits(handler.get_throughput_recipe().rate_exponent, 4.0f));
    assert(handler.is_throughput_metric());
    assert(!handler.uses_throughput_history());

    // A metric of the round robin kind keeps the turn logic of the handler.
    metric_entry turns;
    turns.id = 1002;
    turns.name = "test_turns";
    turns.round_robin = true;
    turns.score = [](const metric_info &, double, int) { return 0.0f; };
    metric_registry::register_metric(turns);

    metric_handler rotating(1002);
    assert(rotating.is_rr());
    assert(!rotating.is_throughput_metric());
    // The cursor moves before it is read, as it always did: rank 1 has the first group.
    assert(rotating.get_rr_metric(0, 1, 2) == 1.0f);
    assert(rotating.get_rr_metric(0, 0, 2) == 0.0f);
    assert(rotating.get_rr_metric(1, 0, 2) == 1.0f);
    assert(rotating.get_rr_metric(1, 1, 2) == 0.0f);
}

void duplicate_id_is_rejected()
{
    const size_t before = metric_registry::ids().size();

    bool thrown = false;
    try
    {
        metric_registry::register_metric(make_test_entry());
    }
    catch (const std::invalid_argument &e)
    {
        thrown = true;
        assert(std::string(e.what()).find("1000") != std::string::npos);
    }
    assert(thrown);

    // The built-in identifiers are taken as well.
    for (int id = 0; id < 7; id++)
    {
        metric_entry entry = make_test_entry();
        entry.id = id;
        thrown = false;
        try
        {
            metric_registry::register_metric(entry);
        }
        catch (const std::invalid_argument &)
        {
            thrown = true;
        }
        assert(thrown);
    }

    // An entry that cannot score is rejected too.
    metric_entry empty;
    empty.id = 1003;
    thrown = false;
    try
    {
        metric_registry::register_metric(empty);
    }
    catch (const std::invalid_argument &)
    {
        thrown = true;
    }
    assert(thrown);

    assert(metric_registry::ids().size() == before);
    assert(!metric_registry::find(1003).has_value());
}

void unknown_id_is_rejected()
{
    // Used to be round robin without a word. The simulator constructor builds the UEs,
    // and main() turns this exception into the configuration exit status.
    assert(throws_invalid_argument(7));
    assert(throws_invalid_argument(-1));
    assert(throws_invalid_argument(999));
    assert(throws_invalid_argument(1004));

    try
    {
        metric_handler::check_metric(999);
        assert(false);
    }
    catch (const std::invalid_argument &e)
    {
        assert(std::string(e.what()).find("999") != std::string::npos);
    }

    assert(metric_handler::check_metric(METRIC_RR).round_robin);
    assert(metric_handler::check_metric(1000).name == "test_weighted");
    assert(!throws_invalid_argument(METRIC_PF));
}

ue make_test_ue(int id, int metric_type)
{
    ue_model model(
        1, -60.0f, ul_power_mode::fixed, 23.0f, 0.8f, 1, 5, 5, 1.5f, OUTDOOR);

    ue_config ue_cfg;
    ue_cfg.ue_m = model;
    ue_cfg.delta_metric = 1.0f;
    ue_cfg.delay_t_metric = 0.1f;
    ue_cfg.beta_metric = 1.0f;
    ue_cfg.priority = 1.0f;
    ue_cfg.traffic_c.ul_target = 1.0f;
    ue_cfg.traffic_c.dl_target = 1.0f;
    ue_cfg.traffic_c.pkt_size = 12000;
    ue_cfg.mobility_c.init_pos = pos2d(100.0f, 0.0f);
    ue_cfg.mobility_c.random_init = false;
    ue_cfg.mobility_c.max_distance = 1000.0f;
    ue_cfg.mobility_c.speed = 0.0f;

    scenario_config scenario(
        URBAN_MACROCELL,
        20.0f,
        10.0f,
        25.0f,
        getBaseMapPath() + "URBAN_MACROCELL_3.5.json",
        true);

    phy_enb_config phy(
        46.0f, 1, 0.00005f, 1, 3.5e9f, 20000000, 1, metric_type,
        0, 0, 3500.0f, 0.0f, -174.0f, 2.0f, 9.0f, 8.7f, 0.0f, 2.0f, 1);
    phy.n_rbgs = 4;
    phy.n_sc_rbg = 12;

    pdcp_config pdcp;
    harq_config harq(1, 1, 1);
    return ue(
        id, ue_cfg, scenario, phy, pdcp, pdcp, harq, SIM_UE, nullptr, false);
}

void phy_layer_fills_ue_id_and_direction()
{
    ue terminal = make_test_ue(7, 1000);
    terminal.init();
    terminal.add_current_t(0.0);
    terminal.step();

    last_seen = metric_info();
    terminal.get_metric(TX_DL, 0, 1);
    assert(last_seen.ue_id == 7);
    assert(last_seen.tx_dir == TX_DL);

    terminal.get_metric(TX_UL, 0, 1);
    assert(last_seen.ue_id == 7);
    assert(last_seen.tx_dir == TX_UL);
}
} // namespace

int main()
{
    metric_info defaults;
    assert(defaults.ue_id == -1);
    assert(defaults.tx_dir == TX_DL);

    builtins_keep_scores_and_recipes();
    builtin_ids_are_registered_first();
    registered_metric_is_selected();
    configured_and_round_robin_entries();
    duplicate_id_is_rejected();
    unknown_id_is_rejected();
    phy_layer_fills_ue_id_and_direction();
    return 0;
}
