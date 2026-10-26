# PHY Model V2 Five-Profile Validation

- Batch: `phy-final-6770c64-legacy-no-retry-seed20260927`
- Source: `6770c645709517af5da8edf6ca9441f14c670374`
- Seed: `20260927`
- Warm-up excluded: 20 seconds

| Profile | Dir. | Offered | Delivered | Errors | Outage UEs | Zero-delivery windows (10/100/1000 ms) | UE max delivery-gap P50/P95/P99/max (ms) | Grid assigned/effective | Payload/grant | Retx/radio loss | HARQ max queue/age | Conservation residual | Wall time |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| offline_indoor_hotspot_n78_pedestrian | DL | 65.00 | 42.17 | 22.95 | 0 | 0.1% / 0.1% / 0.0% | 0 / 430 / 734 / 810 | 86.7% / 85.9% | 21.2% | 0.00 / 0.37 Mbit/s | 0 / -1000.0 ms | 0 bit | 8.71 s |
| offline_indoor_hotspot_n78_pedestrian | UL | 90.00 | 59.82 | 30.18 | 0 | 12.0% / 0.0% / 0.0% | 20 / 85 / 89 / 90 | 100.0% / 60.3% | 46.1% | 0.00 / 0.48 Mbit/s | 0 / -1000.0 ms | 0 bit | 8.71 s |
| offline_rural_n78_vehicular | DL | 120.01 | 50.96 | 69.03 | 0 | 27.1% / 23.4% / 18.6% | 5100 / 66240 / 94920 / 102090 | 96.1% / 93.9% | 97.0% | 0.00 / 1.13 Mbit/s | 0 / -1000.0 ms | 0 bit | 186.32 s |
| offline_rural_n78_vehicular | UL | 120.01 | 26.27 | 93.77 | 0 | 39.2% / 20.2% / 3.2% | 1990 / 4965 / 5073 / 5100 | 99.9% / 56.4% | 84.5% | 0.00 / 0.68 Mbit/s | 0 / -1000.0 ms | 0 bit | 186.32 s |
| offline_uma_n78_pedestrian | DL | 180.01 | 102.49 | 77.53 | 2 | 38.2% / 21.6% / 10.4% | 70 / 76166 / 76641 / 76760 | 99.0% / 97.7% | 72.7% | 0.00 / 1.32 Mbit/s | 0 / -1000.0 ms | 0 bit | 13.06 s |
| offline_uma_n78_pedestrian | UL | 70.00 | 13.28 | 56.72 | 0 | 62.2% / 35.1% / 28.3% | 2300 / 160000 / 160000 / 160000 | 100.0% / 17.7% | 98.7% | 0.00 / 0.16 Mbit/s | 0 / -1000.0 ms | 0 bit | 13.06 s |
| offline_umi_n258_fwa | DL | 500.02 | 499.97 | 0.04 | 0 | 0.0% / 0.0% / 0.0% | 0 / 0 / 0 / 0 | 99.5% / 99.5% | 39.5% | 0.00 / 0.04 Mbit/s | 0 / -1000.0 ms | 0 bit | 8.43 s |
| offline_umi_n258_fwa | UL | 200.01 | 156.98 | 43.03 | 0 | 11.7% / 0.0% / 0.0% | 20 / 20 / 20 / 20 | 100.0% / 28.8% | 22.2% | 0.00 / 0.09 Mbit/s | 0 / -1000.0 ms | 0 bit | 8.43 s |
| offline_umi_n258_fwa_high_loss | DL | 500.02 | 231.99 | 268.03 | 0 | 1.8% / 0.0% / 0.0% | 0 / 45 / 49 / 50 | 99.9% / 98.8% | 33.6% | 0.00 / 3.21 Mbit/s | 0 / -1000.0 ms | 0 bit | 9.65 s |
| offline_umi_n258_fwa_high_loss | UL | 200.01 | 54.31 | 145.70 | 0 | 29.5% / 16.5% / 8.2% | 30 / 7305 / 9405 / 9930 | 100.0% / 43.5% | 98.8% | 0.00 / 0.63 Mbit/s | 0 / -1000.0 ms | 0 bit | 9.65 s |
| offline_umi_n40_npn | DL | 50.00 | 46.38 | 3.62 | 0 | 10.1% / 0.0% / 0.0% | 20 / 65 / 69 / 70 | 100.0% / 99.4% | 71.7% | 0.00 / 0.31 Mbit/s | 0 / -1000.0 ms | 0 bit | 6.52 s |
| offline_umi_n40_npn | UL | 35.00 | 24.11 | 10.89 | 0 | 32.3% / 0.0% / 0.0% | 20 / 30 / 30 / 30 | 100.0% / 83.1% | 75.9% | 0.00 / 0.18 Mbit/s | 0 / -1000.0 ms | 0 bit | 6.52 s |

Rates are Mbit/s. Delivery-window and delivery-gap statistics exclude UEs classified as permanent PHY outage. A PHY-outage UE has MCS below zero in at least 99% of post-warm-up radio samples.

A zero-delivery window has positive offered traffic and no delivered payload in that non-overlapping window. This avoids interpreting every unassigned TTI as user starvation.

The delivery-gap distribution in the table is the distribution of each non-outage UE's maximum observed gap inside contiguous positive-offer segments. It is not a queue-backlog or scheduler-starvation metric. Pooled gap quantiles remain available in the CSV.

Grid assigned/effective values are fractions of physically available frequency-time units. Per-TTI summaries separate TDD-unavailable, available-but-empty, and assigned-with-zero-effective-payload units.

Payload/grant efficiency is sampled from logged grid grants and is the sum of effective payload bits divided by nominal grant bits.

Retx/radio loss reports retransmitted air bits and terminal radio-dropped payload as Mbit/s. HARQ queue age is measured from the original IP arrival time. The conservation residual is the largest absolute difference between admitted bits and delivered, expired, queue-dropped, radio-dropped, and pending bits.
