# PHY Model V2 Five-Profile Validation

- Batch: `harq-production-sweep-seed20260929`
- Source: `4e4ec5fdd26a74ad9c0b838313f39de4d0c9b021`
- Seed: `20260929`
- Warm-up excluded: 2 seconds

| Profile | Dir. | Offered | Delivered | Errors | Outage UEs | Zero-delivery windows (10/100/1000 ms) | UE max delivery-gap P50/P95/P99/max (ms) | Grid assigned/effective | Payload/grant | Retx/radio loss | HARQ max queue/age | Conservation residual | Wall time |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| offline_indoor_hotspot_n78_pedestrian | DL | 64.97 | 25.50 | 39.47 | 0 | 0.0% / 0.0% / 0.0% | 0 / 0 / 0 / 0 | 96.2% / 95.2% | 13.0% | 0.04 / 0.00 Mbit/s | 3 / 300.0 ms | 0 bit | 2.65 s |
| offline_indoor_hotspot_n78_pedestrian | UL | 89.99 | 58.89 | 30.63 | 0 | 6.7% / 2.4% / 0.6% | 20 / 1460 / 2028 / 2170 | 100.0% / 71.8% | 98.3% | 0.11 / 0.00 Mbit/s | 2 / 300.0 ms | 0 bit | 2.65 s |
| offline_rural_n78_vehicular | DL | 120.00 | 72.06 | 46.43 | 0 | 17.8% / 10.8% / 6.8% | 290 / 6505 / 8397 / 8870 | 99.8% / 69.9% | 62.7% | 1.41 / 0.00 Mbit/s | 54 / 300.0 ms | 0 bit | 18.46 s |
| offline_rural_n78_vehicular | UL | 120.00 | 34.41 | 71.51 | 0 | 26.2% / 9.5% / 0.3% | 290 / 980 / 1196 / 1250 | 100.0% / 59.7% | 62.8% | 0.67 / 0.00 Mbit/s | 19 / 300.0 ms | 0 bit | 18.46 s |
| offline_uma_n78_pedestrian | DL | 179.96 | 124.59 | 55.18 | 3 | 16.6% / 5.6% / 1.0% | 125 / 1350 / 3166 / 3620 | 100.0% / 92.0% | 62.8% | 1.03 / 0.01 Mbit/s | 3 / 300.0 ms | 0 bit | 3.59 s |
| offline_uma_n78_pedestrian | UL | 69.98 | 26.95 | 43.02 | 0 | 42.9% / 31.7% / 24.5% | 260 / 28000 / 28000 / 28000 | 100.0% / 28.4% | 73.4% | 0.14 / 0.00 Mbit/s | 2 / 300.0 ms | 0 bit | 3.59 s |
| offline_umi_n258_fwa | DL | 499.83 | 499.82 | 0.00 | 0 | 0.0% / 0.0% / 0.0% | 0 / 0 / 0 / 0 | 99.5% / 99.5% | 39.6% | 0.05 / 0.00 Mbit/s | 1 / 8.0 ms | 0 bit | 2.40 s |
| offline_umi_n258_fwa | UL | 199.96 | 157.10 | 42.86 | 0 | 10.9% / 0.0% / 0.0% | 20 / 20 / 20 / 20 | 100.0% / 29.3% | 21.9% | 0.06 / 0.00 Mbit/s | 1 / 7.0 ms | 0 bit | 2.40 s |
| offline_umi_n258_fwa_high_loss | DL | 499.83 | 172.85 | 326.96 | 0 | 2.4% / 0.0% / 0.0% | 0 / 45 / 49 / 50 | 99.8% / 97.9% | 38.9% | 1.63 / 0.02 Mbit/s | 5 / 300.0 ms | 0 bit | 2.57 s |
| offline_umi_n258_fwa_high_loss | UL | 199.96 | 25.82 | 174.14 | 0 | 34.0% / 27.2% / 26.6% | 30 / 28000 / 28000 / 28000 | 100.0% / 29.1% | 99.0% | 0.00 / 0.00 Mbit/s | 2 / 300.0 ms | 0 bit | 2.57 s |
| offline_umi_n40_npn | DL | 49.99 | 40.43 | 9.53 | 0 | 15.9% / 0.0% / 0.0% | 20 / 65 / 69 / 70 | 100.0% / 98.6% | 69.6% | 0.04 / 0.00 Mbit/s | 3 / 300.0 ms | 0 bit | 1.84 s |
| offline_umi_n40_npn | UL | 34.99 | 15.79 | 19.21 | 0 | 24.6% / 0.0% / 0.0% | 20 / 40 / 48 / 50 | 100.0% / 93.2% | 98.9% | 0.00 / 0.00 Mbit/s | 1 / 300.0 ms | 0 bit | 1.84 s |

Rates are Mbit/s. Delivery-window and delivery-gap statistics exclude UEs classified as permanent PHY outage. A PHY-outage UE has MCS below zero in at least 99% of post-warm-up radio samples.

A zero-delivery window has positive offered traffic and no delivered payload in that non-overlapping window. This avoids interpreting every unassigned TTI as user starvation.

The delivery-gap distribution in the table is the distribution of each non-outage UE's maximum observed gap inside contiguous positive-offer segments. It is not a queue-backlog or scheduler-starvation metric. Pooled gap quantiles remain available in the CSV.

Grid assigned/effective values are fractions of physically available frequency-time units. Per-TTI summaries separate TDD-unavailable, available-but-empty, and assigned-with-zero-effective-payload units.

Payload/grant efficiency is sampled from logged grid grants and is the sum of effective payload bits divided by nominal grant bits.

Retx/radio loss reports retransmitted air bits and terminal radio-dropped payload as Mbit/s. HARQ queue age is measured from the original IP arrival time. The conservation residual is the largest absolute difference between admitted bits and delivered, expired, queue-dropped, radio-dropped, and pending bits.
