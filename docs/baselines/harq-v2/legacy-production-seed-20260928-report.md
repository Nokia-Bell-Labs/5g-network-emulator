# PHY Model V2 Five-Profile Validation

- Batch: `harq-production-sweep-seed20260928`
- Source: `4e4ec5fdd26a74ad9c0b838313f39de4d0c9b021`
- Seed: `20260928`
- Warm-up excluded: 2 seconds

| Profile | Dir. | Offered | Delivered | Errors | Outage UEs | Zero-delivery windows (10/100/1000 ms) | UE max delivery-gap P50/P95/P99/max (ms) | Grid assigned/effective | Payload/grant | Retx/radio loss | HARQ max queue/age | Conservation residual | Wall time |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| offline_indoor_hotspot_n78_pedestrian | DL | 65.00 | 34.77 | 30.22 | 0 | 0.0% / 0.0% / 0.0% | 0 / 0 / 0 / 0 | 97.4% / 96.1% | 20.2% | 0.06 / 0.00 Mbit/s | 4 / 300.0 ms | 0 bit | 2.57 s |
| offline_indoor_hotspot_n78_pedestrian | UL | 90.00 | 48.58 | 41.42 | 0 | 1.8% / 0.0% / 0.0% | 10 / 15 / 19 / 20 | 100.0% / 68.9% | 99.0% | 0.00 / 0.00 Mbit/s | 2 / 300.0 ms | 0 bit | 2.57 s |
| offline_rural_n78_vehicular | DL | 120.00 | 78.02 | 40.40 | 0 | 27.4% / 22.7% / 17.5% | 290 / 15760 / 18864 / 19640 | 99.9% / 72.8% | 72.2% | 1.19 / 0.00 Mbit/s | 52 / 300.0 ms | 0 bit | 19.00 s |
| offline_rural_n78_vehicular | UL | 120.00 | 39.46 | 64.77 | 0 | 24.2% / 14.3% / 6.2% | 230 / 5045 / 8337 / 9160 | 100.0% / 63.4% | 58.8% | 0.64 / 0.00 Mbit/s | 20 / 300.0 ms | 0 bit | 19.00 s |
| offline_uma_n78_pedestrian | DL | 180.01 | 117.37 | 62.64 | 0 | 15.2% / 5.4% / 4.1% | 30 / 510 / 19678 / 24470 | 100.0% / 97.4% | 60.9% | 0.59 / 0.01 Mbit/s | 5 / 300.0 ms | 0 bit | 3.77 s |
| offline_uma_n78_pedestrian | UL | 70.00 | 23.85 | 46.19 | 0 | 31.3% / 13.2% / 8.3% | 30 / 6500 / 10740 / 11800 | 100.0% / 34.4% | 95.1% | 0.00 / 0.00 Mbit/s | 1 / 300.0 ms | 0 bit | 3.77 s |
| offline_umi_n258_fwa | DL | 499.97 | 499.97 | 0.00 | 0 | 0.0% / 0.0% / 0.0% | 0 / 0 / 0 / 0 | 99.5% / 99.5% | 39.7% | 0.06 / 0.00 Mbit/s | 2 / 8.0 ms | 0 bit | 2.46 s |
| offline_umi_n258_fwa | UL | 199.99 | 158.84 | 41.16 | 0 | 12.7% / 0.0% / 0.0% | 20 / 20 / 20 / 20 | 100.0% / 29.8% | 22.0% | 0.10 / 0.00 Mbit/s | 2 / 300.0 ms | 0 bit | 2.46 s |
| offline_umi_n258_fwa_high_loss | DL | 499.97 | 185.29 | 314.68 | 0 | 10.4% / 0.2% / 0.0% | 0 / 225 / 269 / 280 | 99.3% / 97.2% | 36.3% | 1.04 / 0.01 Mbit/s | 4 / 300.0 ms | 0 bit | 2.73 s |
| offline_umi_n258_fwa_high_loss | UL | 199.99 | 26.89 | 173.11 | 1 | 37.8% / 27.1% / 20.0% | 40 / 28000 / 28000 / 28000 | 100.0% / 21.7% | 98.9% | 0.00 / 0.00 Mbit/s | 2 / 300.0 ms | 0 bit | 2.73 s |
| offline_umi_n40_npn | DL | 50.00 | 41.84 | 8.15 | 0 | 17.3% / 0.0% / 0.0% | 40 / 55 / 59 / 60 | 100.0% / 98.6% | 70.2% | 0.03 / 0.00 Mbit/s | 2 / 300.0 ms | 0 bit | 1.41 s |
| offline_umi_n40_npn | UL | 35.00 | 17.36 | 17.64 | 0 | 15.6% / 0.0% / 0.0% | 20 / 25 / 29 / 30 | 100.0% / 94.0% | 98.9% | 0.00 / 0.00 Mbit/s | 1 / 300.0 ms | 0 bit | 1.41 s |

Rates are Mbit/s. Delivery-window and delivery-gap statistics exclude UEs classified as permanent PHY outage. A PHY-outage UE has MCS below zero in at least 99% of post-warm-up radio samples.

A zero-delivery window has positive offered traffic and no delivered payload in that non-overlapping window. This avoids interpreting every unassigned TTI as user starvation.

The delivery-gap distribution in the table is the distribution of each non-outage UE's maximum observed gap inside contiguous positive-offer segments. It is not a queue-backlog or scheduler-starvation metric. Pooled gap quantiles remain available in the CSV.

Grid assigned/effective values are fractions of physically available frequency-time units. Per-TTI summaries separate TDD-unavailable, available-but-empty, and assigned-with-zero-effective-payload units.

Payload/grant efficiency is sampled from logged grid grants and is the sum of effective payload bits divided by nominal grant bits.

Retx/radio loss reports retransmitted air bits and terminal radio-dropped payload as Mbit/s. HARQ queue age is measured from the original IP arrival time. The conservation residual is the largest absolute difference between admitted bits and delivered, expired, queue-dropped, radio-dropped, and pending bits.
