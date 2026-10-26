#include <algorithm>

#include <mobility_models/clockwise_model.h>
#include <mobility_models/linear_drift_model.h>
#include <mobility_models/manhattan_model.h>
#include <mobility_models/mobility_model.h>
#include <mobility_models/random_walk_model.h>
#include <mobility_models/random_waypoint_model.h>
#include <phy_layer/phy_l_definitions.h>



mobility_model::mobility_model(int id, mobility_config mobility_c, int scenario, float maxApothem)
{
    limit_distance(mobility_c.max_distance, scenario);
    limit_speed(mobility_c.speed, scenario);

    if (mobility_c.type == RANDOM_WALK_MODEL)
    {
        mob_model = std::unique_ptr<mobility_model_base>(new random_walk_model(id, mobility_c.init_pos, mobility_c.random_init, mobility_c.speed, mobility_c.speed_var, mobility_c.max_distance, mobility_c.time_target, maxApothem, mobility_c.time_target_var, mobility_c.random_v));
    }
    else if (mobility_c.type == RANDOM_WAYPOINT_MODEL)
    {
        mob_model = std::unique_ptr<mobility_model_base>(new random_waypoint_model(id, mobility_c.init_pos, mobility_c.random_init, mobility_c.speed, mobility_c.speed_var, mobility_c.max_distance, mobility_c.time_target, maxApothem, mobility_c.time_target_var, mobility_c.random_v));
    }
    else if (mobility_c.type == RANDOM_MANHATTAN_MODEL)
    {
        mob_model = std::unique_ptr<mobility_model_base>(new manhattan_model(id, mobility_c.init_pos, mobility_c.random_init, mobility_c.speed, mobility_c.speed_var, mobility_c.max_distance, mobility_c.time_target, maxApothem, mobility_c.time_target_var, mobility_c.random_v));
    }
    else if (mobility_c.type == LINEAR_DRIFT_MODEL)
    {
        mob_model = std::unique_ptr<mobility_model_base>(new linear_drift_model(id, mobility_c.init_pos, mobility_c.random_init, mobility_c.speed, mobility_c.speed_var, mobility_c.max_distance, mobility_c.time_target, maxApothem, mobility_c.time_target_var, mobility_c.random_v));
    }
    else if (mobility_c.type == CLOCKWISE_MODEL)
    {
        mob_model = std::unique_ptr<mobility_model_base>(new clockwise_model(id, mobility_c.init_pos, mobility_c.random_init, mobility_c.speed, mobility_c.speed_var, mobility_c.max_distance, mobility_c.time_target, maxApothem, mobility_c.time_target_var, mobility_c.random_v));
    }
    else
    {
        mob_model = std::unique_ptr<mobility_model_base>(new mobility_model_base(id, mobility_c.init_pos, mobility_c.random_init, mobility_c.speed, mobility_c.speed_var, mobility_c.max_distance, mobility_c.time_target, maxApothem, mobility_c.time_target_var, mobility_c.random_v));
    }
}

mobility_model::~mobility_model() = default;
mobility_model::mobility_model(mobility_model&&) noexcept = default;
mobility_model& mobility_model::operator=(mobility_model&&) noexcept = default;

void mobility_model::update_pos(float t)
{
    mob_model->update_pos(t);
}

void mobility_model::set_initial_position(float _max_apothem)
{
    mob_model->set_random_initial_position(_max_apothem);
}

void mobility_model::set_pos(float x, float y)
{
    mob_model->set_pos(x, y);
}

void mobility_model::set_speed(float speed_ms)
{
    mob_model->set_speed(speed_ms);
}

float mobility_model::get_speed()
{
    return mob_model->get_speed();
}

float mobility_model::get_apothem()
{
    return mob_model->get_apothem();
}

float mobility_model::get_distance()
{
    return mob_model->get_current_d();
}

void mobility_model::get_pos(float &x, float &y)
{
    mob_model->get_pos(x, y);
}

pos2d *mobility_model::get_pos()
{
    return mob_model->get_pos();
}

float mobility_model::get_max_speed()
{
    return mob_model->get_max_speed();
}

float mobility_model::x()
{
    return mob_model->x();
}

float mobility_model::y()
{
    return mob_model->y();
}

void mobility_model::limit_distance(float &distance, int scenario)
{
    switch (scenario)
    {
    case RURAL_MACROCELL:
        distance = std::min(distance, 5000.0f);
        break;
    case URBAN_MACROCELL:
        distance = std::min(distance, 5000.0f);
        break;
    case URBAN_MICROCELL:
        distance = std::min(distance, 5000.0f);
        break;
    case INDOOR_OPEN_OFFICE:
        distance = std::min(distance, 150.0f);
        break;
    case INDOOR_MIXED_OFFICE:
        distance = std::min(distance, 150.0f);
        break;
    case INDOOR_SHOPPING_MALL:
        distance = std::min(distance, 600.0f);
        break;
    }
}

void mobility_model::limit_speed(float &speed, int scenario)
{
    float conversion_factor = 1000.0f / 3600.0f;
    switch (scenario)
    {
    case RURAL_MACROCELL:
        speed = std::min(speed, 120.0f * conversion_factor);
        break;
    default:
        speed = std::min(speed, 3.0f * conversion_factor);
        break;
    }
}
