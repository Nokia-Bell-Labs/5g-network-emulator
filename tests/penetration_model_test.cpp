#include <cassert>
#include <cmath>

#include <phy_layer/penetration_model.h>

namespace
{
bool near(float actual, float expected, float tolerance = 0.1f)
{
    return std::fabs(actual - expected) <= tolerance;
}
} // namespace

int main()
{
    assert(near(
        building_penetration_loss_db(
            PENETRATION_NONE, 3.5f, 0.0f),
        0.0f));
    assert(near(
        building_penetration_loss_db(
            PENETRATION_LOW_LOSS, 3.5f, 0.0f),
        12.7f));
    assert(near(
        building_penetration_loss_db(
            PENETRATION_HIGH_LOSS, 26.0f, 0.0f),
        37.35f));
    assert(near(
        vehicle_penetration_loss_db(
            VEHICLE_STANDARD, 0.0f),
        9.0f));
    assert(near(
        vehicle_penetration_loss_db(
            VEHICLE_METALLIZED, 0.0f),
        20.0f));
    assert(near(
        vehicle_penetration_loss_db(
            VEHICLE_STANDARD, -3.0f),
        0.0f));
    return 0;
}
