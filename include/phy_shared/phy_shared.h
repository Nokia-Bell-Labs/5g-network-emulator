#ifndef PHY_SHARED_H
#define PHY_SHARED_H

#include <algorithm>
#include <array>
#include <random>
#include <string>
#include <mobility_models/pos2d.h>
#include <ue/ue_config.h>

class phy_shared
{
public:
    phy_shared(int _ue_id, ue_config ue_c, scenario_config _scenario_c, phy_enb_config _phy_enb_config);

private:
    std::mt19937 gen;
    std::uniform_real_distribution<float> uniform_stochastics{0.0, 1.0};
    std::normal_distribution<float> normal_stochastics{0.0, 1.0};
    std::array<float, 100> indoor_depth_candidates{};

public:
    int compute_outdoor_to_indoor();
    int verify_outdoor_to_indoor();
    float get_correlation_distance();
    int get_o2i();
    float get_building_penetration_sample() const;
    float get_vehicle_penetration_sample() const;
    float get_indoor_depth(float distance) const;

    
public:

protected:
    int scenario;
    float freq_ghz;
    int n_antennas;
    float correlation_distance;
    int o2i;
    pos2d prev_pos;
    pos2d pos;
    float c_dist;
    bool update_cs = true;
    bool stochastics = true;
    float eNB_h;
    float building_penetration_sample = 0.0f;
    float vehicle_penetration_sample = 0.0f;
};

#endif