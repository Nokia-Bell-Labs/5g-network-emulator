#ifndef CLOCKWISE_MODEL_H
#define CLOCKWISE_MODEL_H

#include <mobility_models/mobility_model_base.h>
#include <cmath>

class clockwise_model : public mobility_model_base
{
public:
    clockwise_model(int id, pos2d _init_pos, bool _random_init, float _speed, float _speed_var,
                    float _max_distance, float _time_target, float maxApothem,
                    float _time_target_var, bool _random_v)
        : mobility_model_base(id, _init_pos, _random_init, _speed, _speed_var, _max_distance,
                              _time_target, maxApothem, _time_target_var, _random_v)
    {
        radius = current_pos.get_distance();
        angle = atan2(current_pos._y(), current_pos._x());
    }

public:
    // The orbit is defined by the radius and the angle, both derived from the position:
    // moving the UE has to rebuild them or it would snap back to the old circle.
    void set_pos(float x, float y) override
    {
        mobility_model_base::set_pos(x, y);
        radius = current_pos.get_distance();
        angle = atan2(current_pos._y(), current_pos._x());
    }

    void update_pos(float current_t)
    {
        if (current_t >= t_target)
        {
            float var = new_speed();
            speed += var;
            t_target += time_target + fabs(var * time_target_var);
        }

        if (past_t > 0)
        {
            float t_step = current_t - past_t;

            // Angular velocity (rad/s) = linear speed / radius
            float omega = speed / radius;

            // Move clockwise (reduce angle)
            angle -= omega * t_step;

            // Keep angle in [-π, π] range
            if (angle < -M_PI) angle += 2 * M_PI;
            if (angle > M_PI) angle -= 2 * M_PI;

            float x = radius * cos(angle);
            float y = radius * sin(angle);

            if (fabs(x) <= max_apothem && fabs(y) <= max_apothem)
            {
                current_pos = pos2d(x, y);
            }
            else
            {
                speed = 0.0f;
            }
        }

        past_t = current_t;
    }

private:
    float radius;
    float angle;
};

#endif
