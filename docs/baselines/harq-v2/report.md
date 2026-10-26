# HARQ Campaign Evidence

All rates are Mbit/s. Each row uses identical profile geometry, traffic, mobility, and seed within its paired campaign.

The disabled/no-retry/production pair and observable production run last 180 seconds. The two additional seed sweeps last 30 seconds with a 2-second warm-up and are sensitivity checks, not direct long-run estimates.

| Profile | Direction | disabled | legacy-no-retry | legacy-production-paired | legacy-production-observable | legacy-production-seed-20260928 | legacy-production-seed-20260929 |
|---|---|---:|---:|---:|---:|---:|---:|
| offline_indoor_hotspot_n78_pedestrian | DL | 42.54 | 42.17 | 42.20 | 42.20 | 34.77 | 25.50 |
| offline_indoor_hotspot_n78_pedestrian | UL | 60.36 | 59.82 | 59.78 | 59.78 | 48.58 | 58.89 |
| offline_rural_n78_vehicular | DL | 52.00 | 50.87 | 45.89 | 45.89 | 78.02 | 72.06 |
| offline_rural_n78_vehicular | UL | 26.32 | 25.66 | 23.26 | 23.26 | 39.46 | 34.41 |
| offline_uma_n78_pedestrian | DL | 103.80 | 102.49 | 102.29 | 102.29 | 117.37 | 124.59 |
| offline_uma_n78_pedestrian | UL | 13.37 | 13.28 | 13.30 | 13.30 | 23.85 | 26.95 |
| offline_umi_n258_fwa | DL | 500.02 | 499.97 | 500.02 | 500.02 | 499.97 | 499.82 |
| offline_umi_n258_fwa | UL | 157.05 | 156.98 | 157.05 | 157.05 | 158.84 | 157.10 |
| offline_umi_n258_fwa_high_loss | DL | 235.16 | 231.99 | 231.27 | 231.27 | 185.29 | 172.85 |
| offline_umi_n258_fwa_high_loss | UL | 55.31 | 54.31 | 54.40 | 54.40 | 26.89 | 25.82 |
| offline_umi_n40_npn | DL | 46.55 | 46.38 | 46.47 | 46.47 | 41.84 | 40.43 |
| offline_umi_n40_npn | UL | 24.30 | 24.11 | 24.17 | 24.17 | 17.36 | 15.79 |

## Observable production runs

| Campaign | Maximum retransmission rate | Maximum radio-drop rate | Maximum HARQ queue | Maximum absolute closure residual |
|---|---:|---:|---:|---:|
| legacy-production-observable | 1.13 Mbit/s | 0.02 Mbit/s | 71 blocks | 0 bit |
| legacy-production-seed-20260928 | 1.19 Mbit/s | 0.01 Mbit/s | 52 blocks | 0 bit |
| legacy-production-seed-20260929 | 1.63 Mbit/s | 0.02 Mbit/s | 54 blocks | 0 bit |

The embedded legacy BLER table is reproducible but not externally calibrated. These results characterize this exact implementation; they do not establish deployment BLER.
