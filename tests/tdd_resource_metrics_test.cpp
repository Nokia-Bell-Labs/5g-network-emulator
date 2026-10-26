#include <cassert>
#include <vector>

#include <common/direction.h>
#include <mac_layer/resource_grid.h>
#include <simulator/configuration_loader.h>

namespace
{
grid make_grid(int direction, int duplexing_type)
{
    return grid(
        direction,
        1,
        1,
        12,
        14,
        METRIC_PF,
        20000000,
        SCH_LOCALIZED_MODE,
        0,
        1,
        duplexing_type,
        0.5f,
        tdd_config(7, 3, 54));
}
} // namespace

int main()
{
    std::vector<ue> terminals;
    grid dl = make_grid(TX_DL, TDD);
    grid ul = make_grid(TX_UL, TDD);
    dl.init(&terminals);
    ul.init(&terminals);

    int available_dl = 0;
    int available_ul = 0;
    int structural_dl = 0;
    int structural_ul = 0;
    for (int tti = 0; tti < 20; ++tti)
    {
        dl.step();
        ul.step();
        const grid_step_metrics &dl_metrics = dl.get_last_step_metrics();
        const grid_step_metrics &ul_metrics = ul.get_last_step_metrics();
        const int dl_units = dl.get_n_freq_rbg();
        const int ul_units = ul.get_n_freq_rbg();

        assert(
            dl_metrics.available_rbg_count
                + dl_metrics.structural_unavailable_rbg_count
            == dl_units);
        assert(
            ul_metrics.available_rbg_count
                + ul_metrics.structural_unavailable_rbg_count
            == ul_units);
        assert(
            dl_metrics.empty_rbg_count
            == dl_metrics.available_rbg_count);
        assert(
            ul_metrics.empty_rbg_count
            == ul_metrics.available_rbg_count);
        assert(dl_metrics.effective_rbg_count == 0);
        assert(ul_metrics.effective_rbg_count == 0);
        assert(dl_metrics.zero_effective_rbg_count == 0);
        assert(ul_metrics.zero_effective_rbg_count == 0);
        assert(dl_metrics.utilization_ratio == 0.0f);
        assert(ul_metrics.utilization_ratio == 0.0f);

        available_dl += dl_metrics.available_rbg_count;
        available_ul += ul_metrics.available_rbg_count;
        structural_dl += dl_metrics.structural_unavailable_rbg_count;
        structural_ul += ul_metrics.structural_unavailable_rbg_count;
    }
    assert(available_dl > 0);
    assert(available_ul > 0);
    assert(structural_dl > 0);
    assert(structural_ul > 0);

    grid fdd = make_grid(TX_DL, FDD);
    fdd.init(&terminals);
    fdd.step();
    const grid_step_metrics &fdd_metrics = fdd.get_last_step_metrics();
    assert(fdd_metrics.available_rbg_count == fdd.get_n_freq_rbg());
    assert(fdd_metrics.structural_unavailable_rbg_count == 0);
    assert(fdd_metrics.empty_rbg_count == fdd_metrics.available_rbg_count);

    grid mu4(
        TX_DL,
        1,
        4,
        12,
        14,
        METRIC_PF,
        100000000,
        SCH_LOCALIZED_MODE,
        1,
        1,
        TDD,
        0.5f,
        tdd_config(7, 3, 54));
    assert(mu4.get_n_freq_rb() == 34);
    return 0;
}
