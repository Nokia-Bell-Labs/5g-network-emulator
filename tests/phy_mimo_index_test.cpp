#include <cassert>

#include <phy_layer/phy_l_definitions.h>

int main()
{
    assert(get_mcs_layer_index(1) == 0);
    assert(get_mcs_layer_index(2) == 1);
    assert(get_mcs_layer_index(3) == 2);
    assert(get_mcs_layer_index(4) == 3);
    assert(get_mcs_layer_index(0) == 0);
    assert(get_mcs_layer_index(8) == 3);

    assert(estimate_rank_from_mean_sinr(MODULATION_256, 1, 100.0f) == 1);
    assert(estimate_rank_from_mean_sinr(MODULATION_256, 4, -20.0f) == 1);
    assert(estimate_rank_from_mean_sinr(MODULATION_256, 4, 5.0f) == 2);
    assert(estimate_rank_from_mean_sinr(MODULATION_256, 4, 9.0f) == 3);
    assert(estimate_rank_from_mean_sinr(MODULATION_256, 4, 20.0f) == 4);
    assert(estimate_rank_from_mean_sinr(MODULATION_256, 8, 20.0f) == 4);
    return 0;
}
