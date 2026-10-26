# PHY Model V2 Five-Profile Validation

- Batch: `phy-final-6770c64-legacy-production-seed20260927`
- Source: `6770c645709517af5da8edf6ca9441f14c670374`
- Seed: `20260927`
- Warm-up excluded: 20 seconds

| Profile | Dir. | Offered | Delivered | Errors | Outage UEs | Zero-delivery windows (10/100/1000 ms) | UE max delivery-gap P50/P95/P99/max (ms) | Grid assigned/effective | Payload/grant | Retx/radio loss | HARQ max queue/age | Conservation residual | Wall time |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| offline_indoor_hotspot_n78_pedestrian | DL | 65.00 | 42.25 | 22.87 | 0 | 0.1% / 0.1% / 0.0% | 0 / 430 / 734 / 810 | 87.2% / 86.1% | 21.1% | 0.25 / 0.00 Mbit/s | 14 / 300.0 ms | 0 bit | 8.78 s |
| offline_indoor_hotspot_n78_pedestrian | UL | 90.00 | 60.07 | 29.93 | 0 | 12.0% / 0.0% / 0.0% | 20 / 85 / 89 / 90 | 100.0% / 60.5% | 46.5% | 0.24 / 0.00 Mbit/s | 7 / 300.0 ms | 0 bit | 8.78 s |
| offline_rural_n78_vehicular | DL | 120.01 | 50.07 | 69.90 | 0 | 27.1% / 23.4% / 18.6% | 5100 / 66240 / 94920 / 102090 | 98.9% / 87.8% | 80.5% | 0.81 / 0.00 Mbit/s | 867 / 300.0 ms | 0 bit | 177.57 s |
| offline_rural_n78_vehicular | UL | 120.01 | 26.64 | 93.41 | 0 | 39.2% / 20.2% / 3.2% | 1990 / 4740 / 5028 / 5100 | 99.9% / 56.8% | 84.9% | 0.49 / 0.00 Mbit/s | 413 / 300.0 ms | 0 bit | 177.57 s |
| offline_uma_n78_pedestrian | DL | 180.01 | 102.71 | 77.27 | 2 | 38.2% / 21.6% / 10.4% | 70 / 76166 / 76641 / 76760 | 99.2% / 97.3% | 72.0% | 0.90 / 0.01 Mbit/s | 18 / 300.0 ms | 0 bit | 13.08 s |
| offline_uma_n78_pedestrian | UL | 70.00 | 13.31 | 56.69 | 0 | 62.1% / 35.0% / 28.1% | 2180 / 160000 / 160000 / 160000 | 100.0% / 17.7% | 98.8% | 0.00 / 0.00 Mbit/s | 1 / 300.0 ms | 0 bit | 13.08 s |
| offline_umi_n258_fwa | DL | 500.02 | 500.02 | 0.00 | 0 | 0.0% / 0.0% / 0.0% | 0 / 0 / 0 / 0 | 99.5% / 99.5% | 39.5% | 0.05 / 0.00 Mbit/s | 2 / 11.0 ms | 0 bit | 8.36 s |
| offline_umi_n258_fwa | UL | 200.01 | 157.05 | 42.96 | 0 | 11.7% / 0.0% / 0.0% | 20 / 20 / 20 / 20 | 100.0% / 28.8% | 22.1% | 0.05 / 0.00 Mbit/s | 2 / 300.0 ms | 0 bit | 8.36 s |
| offline_umi_n258_fwa_high_loss | DL | 500.02 | 231.52 | 268.50 | 0 | 1.8% / 0.0% / 0.0% | 0 / 40 / 40 / 40 | 100.0% / 98.6% | 33.3% | 1.14 / 0.02 Mbit/s | 4 / 300.0 ms | 0 bit | 9.68 s |
| offline_umi_n258_fwa_high_loss | UL | 200.01 | 54.97 | 145.04 | 0 | 29.0% / 16.4% / 7.8% | 30 / 9800 / 14392 / 15540 | 100.0% / 43.3% | 98.9% | 0.00 / 0.00 Mbit/s | 2 / 300.0 ms | 0 bit | 9.68 s |
| offline_umi_n40_npn | DL | 50.00 | 46.49 | 3.52 | 0 | 10.1% / 0.0% / 0.0% | 20 / 65 / 69 / 70 | 100.0% / 99.4% | 72.1% | 0.13 / 0.00 Mbit/s | 4 / 300.0 ms | 0 bit | 6.47 s |
| offline_umi_n40_npn | UL | 35.00 | 24.20 | 10.80 | 0 | 32.1% / 0.0% / 0.0% | 30 / 30 / 30 / 30 | 100.0% / 83.4% | 76.1% | 0.08 / 0.00 Mbit/s | 4 / 300.0 ms | 0 bit | 6.47 s |

Rates are Mbit/s. Delivery-window and delivery-gap statistics exclude UEs classified as permanent PHY outage. A PHY-outage UE has MCS below zero in at least 99% of post-warm-up radio samples.

A zero-delivery window has positive offered traffic and no delivered payload in that non-overlapping window. This avoids interpreting every unassigned TTI as user starvation.

The delivery-gap distribution in the table is the distribution of each non-outage UE's maximum observed gap inside contiguous positive-offer segments. It is not a queue-backlog or scheduler-starvation metric. Pooled gap quantiles remain available in the CSV.

Grid assigned/effective values are fractions of physically available frequency-time units. Per-TTI summaries separate TDD-unavailable, available-but-empty, and assigned-with-zero-effective-payload units.

Payload/grant efficiency is sampled from logged grid grants and is the sum of effective payload bits divided by nominal grant bits.

Retx/radio loss reports retransmitted air bits and terminal radio-dropped payload as Mbit/s. HARQ queue age is measured from the original IP arrival time. The conservation residual is the largest absolute difference between admitted bits and delivered, expired, queue-dropped, radio-dropped, and pending bits.
