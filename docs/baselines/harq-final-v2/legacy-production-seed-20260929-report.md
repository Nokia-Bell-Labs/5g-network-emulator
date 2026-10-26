# PHY Model V2 Five-Profile Validation

- Batch: `phy-final-6770c64-production-seed20260929`
- Source: `6770c645709517af5da8edf6ca9441f14c670374`
- Seed: `20260929`
- Warm-up excluded: 2 seconds

| Profile | Dir. | Offered | Delivered | Errors | Outage UEs | Zero-delivery windows (10/100/1000 ms) | UE max delivery-gap P50/P95/P99/max (ms) | Grid assigned/effective | Payload/grant | Retx/radio loss | HARQ max queue/age | Conservation residual | Wall time |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| offline_indoor_hotspot_n78_pedestrian | DL | 64.97 | 25.50 | 39.47 | 0 | 0.0% / 0.0% / 0.0% | 0 / 0 / 0 / 0 | 96.2% / 95.2% | 13.0% | 0.04 / 0.00 Mbit/s | 3 / 300.0 ms | 0 bit | 2.30 s |
| offline_indoor_hotspot_n78_pedestrian | UL | 89.99 | 59.19 | 30.30 | 0 | 6.3% / 2.3% / 0.6% | 10 / 1475 / 2031 / 2170 | 100.0% / 72.0% | 97.3% | 0.12 / 0.00 Mbit/s | 2 / 300.0 ms | 0 bit | 2.30 s |
| offline_rural_n78_vehicular | DL | 120.00 | 75.85 | 44.10 | 0 | 11.8% / 9.2% / 6.8% | 20 / 6505 / 8397 / 8870 | 98.7% / 80.5% | 74.5% | 1.58 / 0.00 Mbit/s | 601 / 300.0 ms | 0 bit | 24.12 s |
| offline_rural_n78_vehicular | UL | 120.00 | 39.29 | 80.77 | 0 | 24.2% / 9.1% / 0.3% | 220 / 980 / 1196 / 1250 | 100.0% / 69.1% | 74.5% | 0.58 / 0.00 Mbit/s | 281 / 300.0 ms | 0 bit | 24.12 s |
| offline_uma_n78_pedestrian | DL | 179.96 | 129.06 | 50.56 | 3 | 14.0% / 5.0% / 0.6% | 15 / 809 / 1986 / 2280 | 100.0% / 97.9% | 64.4% | 1.08 / 0.01 Mbit/s | 7 / 300.0 ms | 0 bit | 3.33 s |
| offline_uma_n78_pedestrian | UL | 69.98 | 27.00 | 42.98 | 0 | 42.1% / 31.7% / 24.7% | 20 / 28000 / 28000 / 28000 | 100.0% / 28.9% | 73.4% | 0.18 / 0.00 Mbit/s | 2 / 300.0 ms | 0 bit | 3.33 s |
| offline_umi_n258_fwa | DL | 499.83 | 499.82 | 0.00 | 0 | 0.0% / 0.0% / 0.0% | 0 / 0 / 0 / 0 | 99.5% / 99.5% | 39.6% | 0.05 / 0.00 Mbit/s | 1 / 8.0 ms | 0 bit | 2.31 s |
| offline_umi_n258_fwa | UL | 199.96 | 157.11 | 42.86 | 0 | 10.9% / 0.0% / 0.0% | 20 / 20 / 20 / 20 | 100.0% / 29.3% | 21.8% | 0.05 / 0.00 Mbit/s | 2 / 7.0 ms | 0 bit | 2.31 s |
| offline_umi_n258_fwa_high_loss | DL | 499.83 | 172.86 | 326.95 | 0 | 2.5% / 0.0% / 0.0% | 0 / 40 / 48 / 50 | 99.8% / 97.9% | 39.1% | 1.63 / 0.02 Mbit/s | 7 / 300.0 ms | 0 bit | 2.49 s |
| offline_umi_n258_fwa_high_loss | UL | 199.96 | 26.13 | 173.83 | 0 | 33.8% / 27.2% / 26.6% | 30 / 28000 / 28000 / 28000 | 100.0% / 29.4% | 99.4% | 0.00 / 0.00 Mbit/s | 2 / 300.0 ms | 0 bit | 2.49 s |
| offline_umi_n40_npn | DL | 49.99 | 40.43 | 9.52 | 0 | 15.8% / 0.0% / 0.0% | 20 / 60 / 68 / 70 | 100.0% / 98.8% | 70.0% | 0.04 / 0.00 Mbit/s | 3 / 300.0 ms | 0 bit | 1.79 s |
| offline_umi_n40_npn | UL | 34.99 | 15.81 | 19.19 | 0 | 24.6% / 0.0% / 0.0% | 20 / 40 / 48 / 50 | 100.0% / 93.4% | 98.8% | 0.00 / 0.00 Mbit/s | 2 / 300.0 ms | 0 bit | 1.79 s |

Rates are Mbit/s. Delivery-window and delivery-gap statistics exclude UEs classified as permanent PHY outage. A PHY-outage UE has MCS below zero in at least 99% of post-warm-up radio samples.

A zero-delivery window has positive offered traffic and no delivered payload in that non-overlapping window. This avoids interpreting every unassigned TTI as user starvation.

The delivery-gap distribution in the table is the distribution of each non-outage UE's maximum observed gap inside contiguous positive-offer segments. It is not a queue-backlog or scheduler-starvation metric. Pooled gap quantiles remain available in the CSV.

Grid assigned/effective values are fractions of physically available frequency-time units. Per-TTI summaries separate TDD-unavailable, available-but-empty, and assigned-with-zero-effective-payload units.

Payload/grant efficiency is sampled from logged grid grants and is the sum of effective payload bits divided by nominal grant bits.

Retx/radio loss reports retransmitted air bits and terminal radio-dropped payload as Mbit/s. HARQ queue age is measured from the original IP arrival time. The conservation residual is the largest absolute difference between admitted bits and delivered, expired, queue-dropped, radio-dropped, and pending bits.
