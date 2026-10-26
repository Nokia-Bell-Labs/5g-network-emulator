# PHY Model V2 Five-Profile Validation

- Batch: `harq-disabled-seed20260927`
- Source: `7a0c8d773e18fec3531e40892a8156bda4350c12`
- Seed: `20260927`
- Warm-up excluded: 20 seconds

| Profile | Dir. | Offered | Delivered | Errors | Outage UEs | Zero-delivery windows (10/100/1000 ms) | UE max delivery-gap P50/P95/P99/max (ms) | Grid assigned/effective | Payload/grant | Wall time |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| offline_indoor_hotspot_n78_pedestrian | DL | 65.00 | 42.54 | 22.58 | 0 | 0.1% / 0.1% / 0.0% | 0 / 430 / 734 / 810 | 86.7% / 86.7% | 21.4% | 9.92 s |
| offline_indoor_hotspot_n78_pedestrian | UL | 90.00 | 60.36 | 29.64 | 0 | 11.8% / 0.0% / 0.0% | 20 / 85 / 89 / 90 | 100.0% / 60.8% | 46.4% | 9.92 s |
| offline_rural_n78_vehicular | DL | 120.01 | 52.00 | 67.80 | 0 | 27.1% / 23.4% / 18.6% | 5100 / 66240 / 94920 / 102090 | 96.0% / 96.0% | 99.1% | 102.47 s |
| offline_rural_n78_vehicular | UL | 120.01 | 26.32 | 91.10 | 0 | 39.1% / 20.1% / 3.2% | 1990 / 4965 / 5073 / 5100 | 99.9% / 57.2% | 85.1% | 102.47 s |
| offline_uma_n78_pedestrian | DL | 180.01 | 103.80 | 76.22 | 2 | 38.2% / 21.6% / 10.4% | 70 / 76166 / 76641 / 76760 | 99.0% / 99.0% | 73.5% | 14.23 s |
| offline_uma_n78_pedestrian | UL | 70.00 | 13.37 | 56.63 | 0 | 62.0% / 35.0% / 28.3% | 2450 / 160000 / 160000 / 160000 | 100.0% / 17.8% | 100.0% | 14.23 s |
| offline_umi_n258_fwa | DL | 500.02 | 500.02 | 0.00 | 0 | 0.0% / 0.0% / 0.0% | 0 / 0 / 0 / 0 | 99.5% / 99.5% | 39.5% | 9.35 s |
| offline_umi_n258_fwa | UL | 200.01 | 157.05 | 42.96 | 0 | 11.6% / 0.0% / 0.0% | 20 / 20 / 20 / 20 | 100.0% / 28.8% | 22.1% | 9.35 s |
| offline_umi_n258_fwa_high_loss | DL | 500.02 | 235.16 | 264.85 | 0 | 1.8% / 0.0% / 0.0% | 0 / 40 / 40 / 40 | 99.9% / 99.9% | 34.0% | 10.87 s |
| offline_umi_n258_fwa_high_loss | UL | 200.01 | 55.31 | 144.70 | 0 | 29.1% / 16.4% / 8.4% | 30 / 7795 / 10287 / 10910 | 100.0% / 43.8% | 100.0% | 10.87 s |
| offline_umi_n40_npn | DL | 50.00 | 46.55 | 3.45 | 0 | 10.1% / 0.0% / 0.0% | 20 / 65 / 69 / 70 | 100.0% / 100.0% | 71.7% | 6.87 s |
| offline_umi_n40_npn | UL | 35.00 | 24.30 | 10.71 | 0 | 31.9% / 0.0% / 0.0% | 30 / 30 / 30 / 30 | 100.0% / 83.8% | 76.2% | 6.87 s |

Rates are Mbit/s. Delivery-window and delivery-gap statistics exclude UEs classified as permanent PHY outage. A PHY-outage UE has MCS below zero in at least 99% of post-warm-up radio samples.

A zero-delivery window has positive offered traffic and no delivered payload in that non-overlapping window. This avoids interpreting every unassigned TTI as user starvation.

The delivery-gap distribution in the table is the distribution of each non-outage UE's maximum observed gap inside contiguous positive-offer segments. It is not a queue-backlog or scheduler-starvation metric. Pooled gap quantiles remain available in the CSV.

Grid assigned/effective values are fractions of physically available frequency-time units. Per-TTI summaries separate TDD-unavailable, available-but-empty, and assigned-with-zero-effective-payload units.

Payload/grant efficiency is sampled from logged grid grants and is the sum of effective payload bits divided by nominal grant bits.
