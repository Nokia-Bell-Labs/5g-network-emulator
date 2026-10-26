# Original Baseline versus Production PHY Model V2

Both inputs use whole-run metrics. This is a characterization of the combined approved configuration, PHY, PF, O2I, and map changes; it is not a one-factor causal attribution.

| Profile | Dir. | Baseline throughput | V2 throughput | Delta | Outage UEs | Maximum non-outage gap | Payload/grant |
|---|---|---:|---:|---:|---:|---:|---:|
| offline_indoor_hotspot_n78_pedestrian | DL | 61.16 | 41.07 | -32.8% | 0 -> 0 | 0 -> 810 ms | 24.9% -> 20.9% |
| offline_indoor_hotspot_n78_pedestrian | UL | 90.00 | 61.77 | -31.4% | 0 -> 0 | 0 -> 490 ms | 57.6% -> 49.6% |
| offline_rural_n78_vehicular | DL | 82.64 | 52.16 | -36.9% | 0 -> 0 | 32120 -> 102090 ms | 97.7% -> 98.7% |
| offline_rural_n78_vehicular | UL | 97.11 | 26.64 | -72.6% | 0 -> 0 | 13380 -> 5100 ms | 98.7% -> 85.1% |
| offline_uma_n78_pedestrian | DL | 60.29 | 104.31 | +73.0% | 3 -> 2 | 180000 -> 96090 ms | 43.8% -> 73.5% |
| offline_uma_n78_pedestrian | UL | 32.32 | 13.49 | -58.3% | 2 -> 0 | 180000 -> 179990 ms | 30.1% -> 100.0% |
| offline_umi_n258_fwa | DL | 51.45 | 499.98 | +871.8% | 5 -> 0 | 10 -> 0 ms | 88.4% -> 39.6% |
| offline_umi_n258_fwa | UL | 40.01 | 156.86 | +292.1% | 6 -> 0 | 1670 -> 530 ms | 92.5% -> 22.0% |
| offline_umi_n40_npn | DL | 14.93 | 46.18 | +209.3% | 0 -> 0 | 124270 -> 280 ms | 20.5% -> 72.1% |
| offline_umi_n40_npn | UL | 18.19 | 24.46 | +34.5% | 0 -> 0 | 130310 -> 320 ms | 48.2% -> 78.7% |

This combined table is not the map-catalog ablation. The paired legacy-v1 versus padded-v2.1 comparison is reported separately in `map-v2.1-paired-profile-comparison.md`.
