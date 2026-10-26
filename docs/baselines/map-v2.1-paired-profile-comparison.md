# Legacy V1 versus Padded V2.1 Map Profiles

Both batches use the same simulator code, profile settings, traffic, mobility, seed, duration, and analysis. Only the explicit map catalog differs. The legacy n40 profile uses its historical nearest 3.5 GHz UMi map; V2.1 uses the exact 2.38 GHz map.

| Profile | Dir. | Legacy throughput | V2.1 throughput | Delta | Median-UE SINR delta | Outage UEs |
|---|---|---:|---:|---:|---:|---:|
| offline_indoor_hotspot_n78_pedestrian | DL | 43.42 | 42.54 | -2.0% | +4.17 dB | 0 -> 0 |
| offline_indoor_hotspot_n78_pedestrian | UL | 59.33 | 60.35 | +1.7% | +0.32 dB | 0 -> 0 |
| offline_rural_n78_vehicular | DL | 45.41 | 51.82 | +14.1% | +1.08 dB | 0 -> 0 |
| offline_rural_n78_vehicular | UL | 25.58 | 26.28 | +2.7% | +0.24 dB | 0 -> 0 |
| offline_uma_n78_pedestrian | DL | 113.49 | 103.80 | -8.5% | -5.45 dB | 4 -> 2 |
| offline_uma_n78_pedestrian | UL | 16.21 | 13.37 | -17.5% | -3.00 dB | 0 -> 0 |
| offline_umi_n258_fwa | DL | 500.02 | 500.02 | +0.0% | -14.56 dB | 0 -> 0 |
| offline_umi_n258_fwa | UL | 159.83 | 157.05 | -1.7% | -13.20 dB | 0 -> 0 |
| offline_umi_n40_npn | DL | 47.74 | 46.55 | -2.5% | +6.69 dB | 0 -> 0 |
| offline_umi_n40_npn | UL | 25.51 | 24.29 | -4.8% | +2.65 dB | 0 -> 0 |

This is a one-factor catalog comparison for the fixed seed. It does not estimate the expected field-performance change over map realizations.
