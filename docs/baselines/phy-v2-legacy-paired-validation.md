# PHY Model V2 Five-Profile Validation

- Batch: `phy-v2-legacy-rebased-1b55ca1-seed20260927`
- Source: `1b55ca191e27a368936476fff33fc622833d8d13`
- Seed: `20260927`
- Warm-up excluded: 20 seconds

| Profile | Dir. | Offered | Delivered | Errors | Outage UEs | Zero-delivery windows (10/100/1000 ms) | UE max delivery-gap P50/P95/P99/max (ms) | Grid assigned/effective | Payload/grant | Wall time |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| offline_indoor_hotspot_n78_pedestrian | DL | 65.00 | 43.42 | 21.69 | 0 | 0.3% / 0.1% / 0.1% | 0 / 990 / 1742 / 1930 | 87.8% / 87.8% | 23.9% | 8.42 s |
| offline_indoor_hotspot_n78_pedestrian | UL | 90.00 | 59.33 | 30.68 | 0 | 11.9% / 0.2% / 0.1% | 20 / 1325 / 2361 / 2620 | 100.0% / 61.5% | 45.6% | 8.42 s |
| offline_rural_n78_vehicular | DL | 120.01 | 45.41 | 74.59 | 0 | 33.3% / 28.4% / 23.1% | 7040 / 55090 / 70146 / 73910 | 91.6% / 91.6% | 98.5% | 110.53 s |
| offline_rural_n78_vehicular | UL | 120.01 | 25.58 | 94.28 | 0 | 48.3% / 25.4% / 5.5% | 2640 / 6090 / 6370 / 6440 | 99.1% / 43.8% | 70.8% | 110.53 s |
| offline_uma_n78_pedestrian | DL | 180.01 | 113.49 | 66.51 | 4 | 19.1% / 9.8% / 4.4% | 20 / 20224 / 32845 / 36000 | 99.9% / 99.9% | 74.6% | 11.36 s |
| offline_uma_n78_pedestrian | UL | 70.00 | 16.21 | 53.78 | 0 | 50.6% / 34.6% / 30.2% | 1090 / 160000 / 160000 / 160000 | 100.0% / 26.1% | 99.3% | 11.36 s |
| offline_umi_n258_fwa | DL | 500.02 | 500.02 | 0.00 | 0 | 0.0% / 0.0% / 0.0% | 0 / 0 / 0 / 0 | 99.5% / 99.5% | 39.5% | 8.27 s |
| offline_umi_n258_fwa | UL | 200.01 | 159.83 | 40.17 | 0 | 13.1% / 0.0% / 0.0% | 20 / 20 / 20 / 20 | 100.0% / 30.0% | 21.9% | 8.27 s |
| offline_umi_n40_npn | DL | 50.00 | 47.74 | 2.20 | 0 | 11.3% / 0.0% / 0.0% | 30 / 55 / 59 / 60 | 100.0% / 100.0% | 73.0% | 6.45 s |
| offline_umi_n40_npn | UL | 35.00 | 25.51 | 9.49 | 0 | 28.7% / 0.0% / 0.0% | 30 / 30 / 30 / 30 | 100.0% / 88.4% | 81.7% | 6.45 s |

Rates are Mbit/s. Delivery-window and delivery-gap statistics exclude UEs classified as permanent PHY outage. A PHY-outage UE has MCS below zero in at least 99% of post-warm-up radio samples.

A zero-delivery window has positive offered traffic and no delivered payload in that non-overlapping window. This avoids interpreting every unassigned TTI as user starvation.

The delivery-gap distribution in the table is the distribution of each non-outage UE's maximum observed gap inside contiguous positive-offer segments. It is not a queue-backlog or scheduler-starvation metric. Pooled gap quantiles remain available in the CSV.

Grid assigned/effective values are fractions of physically available frequency-time units. Per-TTI summaries separate TDD-unavailable, available-but-empty, and assigned-with-zero-effective-payload units.

Payload/grant efficiency is sampled from logged grid grants and is the sum of effective payload bits divided by nominal grant bits.
