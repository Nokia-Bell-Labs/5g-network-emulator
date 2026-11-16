#include <cassert>
#include <cmath>
#include <stdexcept>

#include <mobility_models/mobility_model.h>
#include <phy_layer/phy_l_definitions.h>

namespace
{
bool near(float actual, float expected)
{
    return std::fabs(actual - expected) < 1e-3f;
}

bool inside_yard(float x, float y)
{
    return x >= 12.0f && x <= 80.0f && std::fabs(y) <= 40.0f;
}
} // namespace

int main()
{
    mobility_config drift_cfg(
        LINEAR_DRIFT_MODEL, 20.0f, 0.0f, false, 5.0f, 0.0f, 200.0f, 1.0e6, 0.0);
    mobility_model drift(1, drift_cfg, RURAL_MACROCELL, 500.0f);
    drift.constrain_to_space(
        [](float x, float) { return x >= 12.0f && x <= 25.0f; },
        false);
    drift.update_pos(1.0);
    assert(near(drift.x(), 20.0f));
    drift.update_pos(2.0);
    assert(near(drift.x(), 25.0f));
    drift.update_pos(3.0);
    assert(near(drift.x(), 25.0f));
    assert(near(drift.get_speed(), 0.0f));

    mobility_config walk_cfg(
        RANDOM_WALK_MODEL, 20.0f, 0.0f, false, 5.0f, 0.0f, 200.0f, 1.0, 0.0);
    mobility_model walk(2, walk_cfg, RURAL_MACROCELL, 500.0f);
    walk.constrain_to_space(inside_yard, false);
    for (int step = 1; step <= 40; ++step)
    {
        walk.update_pos(static_cast<double>(step));
        assert(inside_yard(walk.x(), walk.y()));
    }

    walk.set_pos(0.0f, 0.0f);
    assert(near(walk.x(), 0.0f));
    assert(near(walk.y(), 0.0f));
    walk.constrain_to_space(inside_yard, true);
    assert(inside_yard(walk.x(), walk.y()));
    assert(walk.get_distance() > 10.0f);

    mobility_config blocked_cfg(
        LINEAR_DRIFT_MODEL, 20.0f, 0.0f, false, 5.0f, 0.0f, 200.0f, 1.0e6, 0.0);
    mobility_model blocked(3, blocked_cfg, RURAL_MACROCELL, 500.0f);
    bool threw = false;
    try
    {
        blocked.constrain_to_space(
            [](float, float) { return false; },
            true);
    }
    catch (const std::runtime_error &)
    {
        threw = true;
    }
    assert(threw);
    return 0;
}
