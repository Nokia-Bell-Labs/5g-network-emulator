#include <cassert>
#include <cstdint>
#include <memory>
#include <vector>

#include <common/direction.h>
#include <pdcp_layer/pdcp_layer.h>
#include <simulator/configuration_loader.h>
#include <traffic_models/traffic_config.h>

namespace
{
packet_handler_config config(int max_rtx, harq_model model)
{
    packet_handler_config cfg;
    cfg.ue_type = SIM_UE;
    cfg.tx_dir = TX_DL;
    cfg.queue_num = -1;
    cfg.ue_id = 7;
    cfg.init_t = nullptr;
    cfg.traffic_c = traffic_config(
        CONSTANT_TRAFFIC_MODEL,
        0.0f,
        0.0f,
        "",
        "",
        0.0f,
        1000,
        0.0f,
        0.0f);
    cfg.pdcp_c = pdcp_config(
        max_rtx,
        0.0f,
        0.0f,
        0.0f,
        0.0f,
        0.0f,
        0.0f,
        0.0f,
        true,
        model);
    cfg.log_traffic = true;
    cfg.log_quality = false;
    return cfg;
}

pdcp_layer layer_with_packet(int max_rtx, harq_model model)
{
    pdcp_layer layer(config(max_rtx, model), 1);
    layer.init(0, 1, 1);
    assert(layer.inject_bits(1000, 1));
    layer.step(0.0f);
    return layer;
}
} // namespace

int main()
{
    {
        pdcp_layer layer = layer_with_packet(
            4,
            harq_model::legacy_bler);
        layer.set_harq_scripted_outcomes({0.0, 1.0});
        assert(layer.handle_pkt(1000.0f, 27, -20.0f, 0.0f, 1) == 0.0f);
        assert(layer.last_charged_grant_bits() == 1000);
        layer.step(0.001f);

        assert(layer.handle_pkt(500.0f, 27, 40.0f, 0.0f, 1) == 0.0f);
        assert(layer.last_charged_grant_bits() == 0);
        assert(layer.retransmitted_bits_total() == 0);

        const float delivered =
            layer.handle_pkt(1000.0f, 27, 40.0f, 0.0f, 1);
        assert(delivered == 1000.0f);
        assert(layer.last_charged_grant_bits() == 1000);
        assert(layer.retransmitted_bits_total() == 1000);
        assert(layer.release() == 1000.0f);
        assert(layer.delivered_bits_total() == 1000);
        assert(layer.radio_dropped_bits_total() == 0);
        assert(layer.conservation_residual_bits() == 0);
    }

    {
        packet_handler_config saturated_config =
            config(4, harq_model::legacy_bler);
        saturated_config.pdcp_c.rtx_period = 1.0f;
        pdcp_layer layer(saturated_config, 1);
        layer.init(0, 1, 1);
        constexpr std::uint64_t block_count = 4097;
        assert(layer.inject_bits(block_count * 1000, 2));
        layer.step(0.0f);
        layer.set_harq_scripted_outcomes(
            std::vector<double>(block_count, 0.0));
        for (std::uint64_t index = 0;
             index < block_count;
             index++)
            assert(
                layer.handle_pkt(
                    1000.0f,
                    27,
                    -20.0f,
                    0.0f,
                    1)
                == 0.0f);
        const pdcp_queue_status status =
            layer.get_queue_status();
        assert(status.harq_size == 4096);
        assert(status.harq_high_water_blocks == 4096);
        assert(status.harq_capacity_blocks == 4096);
        assert(layer.radio_dropped_bits_total() == 1000);
        assert(layer.conservation_residual_bits() == 0);
    }

    {
        pdcp_layer layer = layer_with_packet(
            0,
            harq_model::legacy_bler);
        layer.set_harq_scripted_outcomes({0.0});
        assert(layer.handle_pkt(1000.0f, 27, -20.0f, 0.0f, 1) == 0.0f);
        assert(layer.radio_dropped_bits_total() == 1000);
        assert(layer.retransmitted_bits_total() == 0);
        assert(layer.conservation_residual_bits() == 0);
    }

    {
        pdcp_layer layer(config(4, harq_model::legacy_bler), 1);
        layer.init(0, 1, 1);
        assert(layer.inject_bits(1500, 3));
        layer.step(0.0f);
        layer.set_harq_scripted_outcomes({0.0, 1.0, 1.0});
        assert(
            layer.handle_pkt(
                1000.0f,
                27,
                -20.0f,
                0.0f,
                1)
            == 0.0f);
        layer.step(0.001f);
        assert(
            layer.handle_pkt(
                500.0f,
                27,
                40.0f,
                0.0f,
                1)
            == 500.0f);
        assert(layer.last_charged_grant_bits() == 500);
        assert(layer.release() == 500.0f);
        assert(
            layer.handle_pkt(
                1000.0f,
                27,
                40.0f,
                0.0f,
                1)
            == 1000.0f);
        assert(layer.retransmitted_bits_total() == 1000);
        assert(layer.release() == 1000.0f);
        assert(layer.conservation_residual_bits() == 0);
    }

    {
        pdcp_layer layer = layer_with_packet(
            1,
            harq_model::legacy_bler);
        layer.set_harq_scripted_outcomes({0.0, 0.0});
        assert(layer.handle_pkt(1000.0f, 27, -20.0f, 0.0f, 1) == 0.0f);
        layer.step(0.001f);
        assert(layer.handle_pkt(1000.0f, 27, -20.0f, 0.0f, 1) == 0.0f);
        assert(layer.radio_dropped_bits_total() == 1000);
        assert(layer.retransmitted_bits_total() == 1000);
        assert(layer.conservation_residual_bits() == 0);
    }

    {
        pdcp_layer layer = layer_with_packet(
            4,
            harq_model::disabled);
        std::uint64_t charged = 0;
        float effective = 0.0f;
        for (int attempt = 0; attempt < 20; attempt++)
        {
            const float current =
                layer.handle_pkt(0.4f, 28, 0.0f, 0.0f, 9);
            assert(
                current
                <= static_cast<float>(
                    layer.last_charged_grant_bits()));
            charged += layer.last_charged_grant_bits();
            effective += current;
            layer.release();
        }
        assert(charged == 8);
        assert(effective == 8.0f);
        assert(layer.delivered_bits_total() == 8);
        assert(layer.conservation_residual_bits() == 0);
    }

    return 0;
}
