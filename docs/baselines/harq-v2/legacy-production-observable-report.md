# PHY Model V2 Five-Profile Validation

- Batch: `harq-production-observable-seed20260927`
- Source: `4e4ec5fdd26a74ad9c0b838313f39de4d0c9b021`
- Seed: `20260927`
- Warm-up excluded: 20 seconds

| Profile | Dir. | Offered | Delivered | Errors | Outage UEs | Zero-delivery windows (10/100/1000 ms) | UE max delivery-gap P50/P95/P99/max (ms) | Grid assigned/effective | Payload/grant | Retx/radio loss | HARQ max queue/age | Conservation residual | Wall time |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| offline_indoor_hotspot_n78_pedestrian | DL | 65.00 | 42.20 | 22.91 | 0 | 0.2% / 0.1% / 0.0% | 10 / 425 / 733 / 810 | 87.3% / 85.8% | 21.1% | 0.24 / 0.00 Mbit/s | 5 / 300.0 ms | 0 bit | 10.08 s |
| offline_indoor_hotspot_n78_pedestrian | UL | 90.00 | 59.78 | 30.23 | 0 | 12.2% / 0.1% / 0.0% | 180 / 275 / 287 / 290 | 100.0% / 59.7% | 46.4% | 0.21 / 0.00 Mbit/s | 4 / 300.0 ms | 0 bit | 10.08 s |
| offline_rural_n78_vehicular | DL | 120.01 | 45.89 | 71.85 | 0 | 30.4% / 24.2% / 18.6% | 5100 / 66240 / 94920 / 102090 | 99.5% / 78.1% | 65.9% | 0.64 / 0.00 Mbit/s | 71 / 300.0 ms | 0 bit | 99.32 s |
| offline_rural_n78_vehicular | UL | 120.01 | 23.26 | 91.54 | 0 | 39.7% / 20.3% / 3.1% | 1990 / 4740 / 5028 / 5100 | 99.9% / 51.6% | 72.6% | 0.43 / 0.00 Mbit/s | 28 / 300.0 ms | 0 bit | 99.32 s |
| offline_uma_n78_pedestrian | DL | 180.01 | 102.29 | 77.58 | 2 | 38.4% / 21.6% / 10.4% | 200 / 76166 / 76641 / 76760 | 99.3% / 96.3% | 71.5% | 0.80 / 0.01 Mbit/s | 4 / 300.0 ms | 0 bit | 14.53 s |
| offline_uma_n78_pedestrian | UL | 70.00 | 13.30 | 56.70 | 0 | 62.1% / 35.0% / 28.2% | 2450 / 160000 / 160000 / 160000 | 100.0% / 17.7% | 99.0% | 0.00 / 0.00 Mbit/s | 2 / 300.0 ms | 0 bit | 14.53 s |
| offline_umi_n258_fwa | DL | 500.02 | 500.02 | 0.00 | 0 | 0.0% / 0.0% / 0.0% | 0 / 0 / 0 / 0 | 99.5% / 99.5% | 39.6% | 0.05 / 0.00 Mbit/s | 3 / 10.0 ms | 0 bit | 9.66 s |
| offline_umi_n258_fwa | UL | 200.01 | 157.05 | 42.96 | 0 | 11.7% / 0.0% / 0.0% | 20 / 20 / 20 / 20 | 100.0% / 28.8% | 22.2% | 0.04 / 0.00 Mbit/s | 2 / 300.0 ms | 0 bit | 9.66 s |
| offline_umi_n258_fwa_high_loss | DL | 500.02 | 231.27 | 268.75 | 0 | 1.9% / 0.0% / 0.0% | 0 / 45 / 49 / 50 | 100.0% / 98.4% | 33.3% | 1.13 / 0.02 Mbit/s | 5 / 300.0 ms | 0 bit | 10.98 s |
| offline_umi_n258_fwa_high_loss | UL | 200.01 | 54.40 | 145.61 | 0 | 29.2% / 16.4% / 8.5% | 30 / 8295 / 11187 / 11910 | 100.0% / 43.5% | 99.0% | 0.00 / 0.00 Mbit/s | 1 / 300.0 ms | 0 bit | 10.98 s |
| offline_umi_n40_npn | DL | 50.00 | 46.47 | 3.54 | 0 | 10.3% / 0.0% / 0.0% | 30 / 105 / 133 / 140 | 100.0% / 98.9% | 72.1% | 0.13 / 0.00 Mbit/s | 4 / 300.0 ms | 0 bit | 7.12 s |
| offline_umi_n40_npn | UL | 35.00 | 24.17 | 10.83 | 0 | 32.2% / 0.0% / 0.0% | 30 / 50 / 66 / 70 | 100.0% / 83.0% | 75.8% | 0.08 / 0.00 Mbit/s | 4 / 300.0 ms | 0 bit | 7.12 s |

Rates are Mbit/s. Delivery-window and delivery-gap statistics exclude UEs classified as permanent PHY outage. A PHY-outage UE has MCS below zero in at least 99% of post-warm-up radio samples.

A zero-delivery window has positive offered traffic and no delivered payload in that non-overlapping window. This avoids interpreting every unassigned TTI as user starvation.

The delivery-gap distribution in the table is the distribution of each non-outage UE's maximum observed gap inside contiguous positive-offer segments. It is not a queue-backlog or scheduler-starvation metric. Pooled gap quantiles remain available in the CSV.

Grid assigned/effective values are fractions of physically available frequency-time units. Per-TTI summaries separate TDD-unavailable, available-but-empty, and assigned-with-zero-effective-payload units.

Payload/grant efficiency is sampled from logged grid grants and is the sum of effective payload bits divided by nominal grant bits.

Retx/radio loss reports retransmitted air bits and terminal radio-dropped payload as Mbit/s. HARQ queue age is measured from the original IP arrival time. The conservation residual is the largest absolute difference between admitted bits and delivered, expired, queue-dropped, radio-dropped, and pending bits.
