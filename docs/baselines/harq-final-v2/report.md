# HARQ Campaign Evidence

All rates are Mbit/s. Each row uses identical profile geometry, traffic, mobility, and seed within its paired campaign.

The disabled, no-retry, and production arms last 180 seconds with a 20-second warm-up. The two additional production seed sweeps last 30 seconds with a 2-second warm-up and are sensitivity checks, not direct long-run estimates.

| Profile | Direction | disabled | legacy-no-retry | legacy-production | legacy-production-seed-20260928 | legacy-production-seed-20260929 |
|---|---|---:|---:|---:|---:|---:|
| offline_indoor_hotspot_n78_pedestrian | DL | 42.54 | 42.17 | 42.25 | 34.76 | 25.50 |
| offline_indoor_hotspot_n78_pedestrian | UL | 60.36 | 59.82 | 60.07 | 48.53 | 59.19 |
| offline_rural_n78_vehicular | DL | 52.09 | 50.96 | 50.07 | 81.63 | 75.85 |
| offline_rural_n78_vehicular | UL | 26.95 | 26.27 | 26.64 | 42.01 | 39.29 |
| offline_uma_n78_pedestrian | DL | 103.80 | 102.49 | 102.71 | 117.74 | 129.06 |
| offline_uma_n78_pedestrian | UL | 13.37 | 13.28 | 13.31 | 23.79 | 27.00 |
| offline_umi_n258_fwa | DL | 500.02 | 499.97 | 500.02 | 499.97 | 499.82 |
| offline_umi_n258_fwa | UL | 157.05 | 156.98 | 157.05 | 158.84 | 157.11 |
| offline_umi_n258_fwa_high_loss | DL | 235.16 | 231.99 | 231.52 | 185.73 | 172.86 |
| offline_umi_n258_fwa_high_loss | UL | 55.31 | 54.31 | 54.97 | 26.83 | 26.13 |
| offline_umi_n40_npn | DL | 46.55 | 46.38 | 46.49 | 41.85 | 40.43 |
| offline_umi_n40_npn | UL | 24.30 | 24.11 | 24.20 | 17.38 | 15.81 |

## HARQ and accounting telemetry

| Campaign | Maximum retransmission rate | Maximum radio-drop rate | Maximum HARQ queue | Maximum absolute closure residual |
|---|---:|---:|---:|---:|
| disabled | 0.00 Mbit/s | 0.00 Mbit/s | 0 blocks | 0 bit |
| legacy-no-retry | 0.00 Mbit/s | 3.21 Mbit/s | 0 blocks | 0 bit |
| legacy-production | 1.14 Mbit/s | 0.02 Mbit/s | 867 blocks | 0 bit |
| legacy-production-seed-20260928 | 1.39 Mbit/s | 0.01 Mbit/s | 688 blocks | 0 bit |
| legacy-production-seed-20260929 | 1.63 Mbit/s | 0.02 Mbit/s | 601 blocks | 0 bit |

The embedded legacy BLER table is reproducible but not externally calibrated. These results characterize this exact implementation; they do not establish deployment BLER.
