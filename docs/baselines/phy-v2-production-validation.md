# PHY Model V2 Five-Profile Validation

- Batch: `phy-v2-rebased-1b55ca1-seed20260927`
- Source: `1b55ca191e27a368936476fff33fc622833d8d13`
- Seed: `20260927`
- Warm-up excluded: 20 seconds

| Profile | Dir. | Offered | Delivered | Errors | Outage UEs | Zero-delivery windows (10/100/1000 ms) | UE max delivery-gap P50/P95/P99/max (ms) | Grid assigned/effective | Payload/grant | Wall time |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| offline_indoor_hotspot_n78_pedestrian | DL | 65.00 | 42.54 | 22.58 | 0 | 0.1% / 0.1% / 0.0% | 0 / 430 / 734 / 810 | 86.7% / 86.7% | 21.4% | 8.42 s |
| offline_indoor_hotspot_n78_pedestrian | UL | 90.00 | 60.35 | 29.65 | 0 | 11.8% / 0.0% / 0.0% | 20 / 85 / 89 / 90 | 100.0% / 60.8% | 46.5% | 8.42 s |
| offline_rural_n78_vehicular | DL | 120.01 | 51.82 | 67.97 | 0 | 27.1% / 23.4% / 18.6% | 5100 / 66240 / 94920 / 102090 | 96.0% / 96.0% | 98.7% | 128.08 s |
| offline_rural_n78_vehicular | UL | 120.01 | 26.28 | 91.13 | 0 | 39.1% / 20.1% / 3.2% | 1990 / 4965 / 5073 / 5100 | 99.9% / 57.3% | 85.0% | 128.08 s |
| offline_uma_n78_pedestrian | DL | 180.01 | 103.80 | 76.21 | 2 | 38.2% / 21.6% / 10.4% | 70 / 76166 / 76641 / 76760 | 99.0% / 99.0% | 73.5% | 11.13 s |
| offline_uma_n78_pedestrian | UL | 70.00 | 13.37 | 56.62 | 0 | 62.0% / 35.0% / 28.2% | 2450 / 160000 / 160000 / 160000 | 100.0% / 17.8% | 100.0% | 11.13 s |
| offline_umi_n258_fwa | DL | 500.02 | 500.02 | 0.00 | 0 | 0.0% / 0.0% / 0.0% | 0 / 0 / 0 / 0 | 99.5% / 99.5% | 39.5% | 8.08 s |
| offline_umi_n258_fwa | UL | 200.01 | 157.05 | 42.96 | 0 | 11.6% / 0.0% / 0.0% | 20 / 20 / 20 / 20 | 100.0% / 28.8% | 22.1% | 8.08 s |
| offline_umi_n40_npn | DL | 50.00 | 46.55 | 3.46 | 0 | 10.1% / 0.0% / 0.0% | 20 / 65 / 69 / 70 | 100.0% / 100.0% | 71.9% | 6.32 s |
| offline_umi_n40_npn | UL | 35.00 | 24.29 | 10.71 | 0 | 31.8% / 0.0% / 0.0% | 30 / 30 / 30 / 30 | 100.0% / 83.8% | 76.5% | 6.32 s |

Rates are Mbit/s. Delivery-window and delivery-gap statistics exclude UEs classified as permanent PHY outage. A PHY-outage UE has MCS below zero in at least 99% of post-warm-up radio samples.

A zero-delivery window has positive offered traffic and no delivered payload in that non-overlapping window. This avoids interpreting every unassigned TTI as user starvation.

The delivery-gap distribution in the table is the distribution of each non-outage UE's maximum observed gap inside contiguous positive-offer segments. It is not a queue-backlog or scheduler-starvation metric. Pooled gap quantiles remain available in the CSV.

Grid assigned/effective values are fractions of physically available frequency-time units. Per-TTI summaries separate TDD-unavailable, available-but-empty, and assigned-with-zero-effective-payload units.

Payload/grant efficiency is sampled from logged grid grants and is the sum of effective payload bits divided by nominal grant bits.
