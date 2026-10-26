# PHY Model V2 Five-Profile Validation

- Batch: `phy-final-6770c64-production-seed20260928`
- Source: `6770c645709517af5da8edf6ca9441f14c670374`
- Seed: `20260928`
- Warm-up excluded: 2 seconds

| Profile | Dir. | Offered | Delivered | Errors | Outage UEs | Zero-delivery windows (10/100/1000 ms) | UE max delivery-gap P50/P95/P99/max (ms) | Grid assigned/effective | Payload/grant | Retx/radio loss | HARQ max queue/age | Conservation residual | Wall time |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| offline_indoor_hotspot_n78_pedestrian | DL | 65.00 | 34.76 | 30.23 | 0 | 0.0% / 0.0% / 0.0% | 0 / 0 / 0 / 0 | 97.4% / 96.1% | 20.2% | 0.07 / 0.00 Mbit/s | 3 / 300.0 ms | 0 bit | 2.32 s |
| offline_indoor_hotspot_n78_pedestrian | UL | 90.00 | 48.53 | 41.47 | 0 | 1.9% / 0.0% / 0.0% | 10 / 10 / 10 / 10 | 100.0% / 68.9% | 99.1% | 0.00 / 0.00 Mbit/s | 1 / 300.0 ms | 0 bit | 2.32 s |
| offline_rural_n78_vehicular | DL | 120.00 | 81.63 | 38.48 | 0 | 22.6% / 21.6% / 17.5% | 0 / 15760 / 18864 / 19640 | 98.8% / 82.0% | 79.2% | 1.39 / 0.00 Mbit/s | 688 / 300.0 ms | 0 bit | 24.25 s |
| offline_rural_n78_vehicular | UL | 120.00 | 42.01 | 77.99 | 0 | 23.0% / 13.8% / 6.2% | 0 / 5080 / 8344 / 9160 | 100.0% / 70.5% | 65.1% | 0.57 / 0.00 Mbit/s | 240 / 300.0 ms | 0 bit | 24.25 s |
| offline_uma_n78_pedestrian | DL | 180.01 | 117.74 | 62.27 | 0 | 14.8% / 5.3% / 3.9% | 30 / 510 / 19222 / 23900 | 100.0% / 98.4% | 60.7% | 0.59 / 0.01 Mbit/s | 4 / 300.0 ms | 0 bit | 3.34 s |
| offline_uma_n78_pedestrian | UL | 70.00 | 23.79 | 46.24 | 0 | 31.8% / 13.4% / 8.7% | 30 / 6230 / 10686 / 11800 | 100.0% / 34.2% | 96.0% | 0.00 / 0.00 Mbit/s | 1 / 300.0 ms | 0 bit | 3.34 s |
| offline_umi_n258_fwa | DL | 499.97 | 499.97 | 0.00 | 0 | 0.0% / 0.0% / 0.0% | 0 / 0 / 0 / 0 | 99.5% / 99.5% | 39.7% | 0.06 / 0.00 Mbit/s | 2 / 8.0 ms | 0 bit | 2.24 s |
| offline_umi_n258_fwa | UL | 199.99 | 158.84 | 41.15 | 0 | 12.7% / 0.0% / 0.0% | 20 / 20 / 20 / 20 | 100.0% / 29.9% | 21.9% | 0.11 / 0.00 Mbit/s | 2 / 300.0 ms | 0 bit | 2.24 s |
| offline_umi_n258_fwa_high_loss | DL | 499.97 | 185.73 | 314.24 | 0 | 10.0% / 0.1% / 0.0% | 0 / 130 / 162 / 170 | 99.3% / 97.8% | 36.5% | 0.99 / 0.01 Mbit/s | 3 / 300.0 ms | 0 bit | 2.43 s |
| offline_umi_n258_fwa_high_loss | UL | 199.99 | 26.83 | 173.16 | 1 | 38.0% / 27.1% / 20.0% | 35 / 28000 / 28000 / 28000 | 100.0% / 21.7% | 99.3% | 0.00 / 0.00 Mbit/s | 1 / 300.0 ms | 0 bit | 2.43 s |
| offline_umi_n40_npn | DL | 50.00 | 41.85 | 8.13 | 0 | 17.2% / 0.0% / 0.0% | 30 / 50 / 50 / 50 | 100.0% / 98.8% | 70.4% | 0.04 / 0.00 Mbit/s | 2 / 300.0 ms | 0 bit | 1.79 s |
| offline_umi_n40_npn | UL | 35.00 | 17.38 | 17.62 | 0 | 15.7% / 0.0% / 0.0% | 20 / 20 / 20 / 20 | 100.0% / 94.0% | 98.5% | 0.00 / 0.00 Mbit/s | 1 / 300.0 ms | 0 bit | 1.79 s |

Rates are Mbit/s. Delivery-window and delivery-gap statistics exclude UEs classified as permanent PHY outage. A PHY-outage UE has MCS below zero in at least 99% of post-warm-up radio samples.

A zero-delivery window has positive offered traffic and no delivered payload in that non-overlapping window. This avoids interpreting every unassigned TTI as user starvation.

The delivery-gap distribution in the table is the distribution of each non-outage UE's maximum observed gap inside contiguous positive-offer segments. It is not a queue-backlog or scheduler-starvation metric. Pooled gap quantiles remain available in the CSV.

Grid assigned/effective values are fractions of physically available frequency-time units. Per-TTI summaries separate TDD-unavailable, available-but-empty, and assigned-with-zero-effective-payload units.

Payload/grant efficiency is sampled from logged grid grants and is the sum of effective payload bits divided by nominal grant bits.

Retx/radio loss reports retransmitted air bits and terminal radio-dropped payload as Mbit/s. HARQ queue age is measured from the original IP arrival time. The conservation residual is the largest absolute difference between admitted bits and delivered, expired, queue-dropped, radio-dropped, and pending bits.
