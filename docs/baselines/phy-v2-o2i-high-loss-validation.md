# PHY Model V2 Five-Profile Validation

- Batch: `phy-v2-o2i-rebased-1b55ca1-seed20260927`
- Source: `1b55ca191e27a368936476fff33fc622833d8d13`
- Seed: `20260927`
- Warm-up excluded: 20 seconds

| Profile | Dir. | Offered | Delivered | Errors | Outage UEs | Zero-delivery windows (10/100/1000 ms) | UE max delivery-gap P50/P95/P99/max (ms) | Grid assigned/effective | Payload/grant | Wall time |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| offline_umi_n258_fwa_high_loss | DL | 500.02 | 235.16 | 264.86 | 0 | 1.8% / 0.0% / 0.0% | 0 / 40 / 40 / 40 | 99.9% / 99.9% | 34.0% | 9.11 s |
| offline_umi_n258_fwa_high_loss | UL | 200.01 | 55.22 | 144.79 | 0 | 29.2% / 16.4% / 8.3% | 30 / 8170 / 10962 / 11660 | 100.0% / 43.8% | 100.0% | 9.11 s |

Rates are Mbit/s. Delivery-window and delivery-gap statistics exclude UEs classified as permanent PHY outage. A PHY-outage UE has MCS below zero in at least 99% of post-warm-up radio samples.

A zero-delivery window has positive offered traffic and no delivered payload in that non-overlapping window. This avoids interpreting every unassigned TTI as user starvation.

The delivery-gap distribution in the table is the distribution of each non-outage UE's maximum observed gap inside contiguous positive-offer segments. It is not a queue-backlog or scheduler-starvation metric. Pooled gap quantiles remain available in the CSV.

Grid assigned/effective values are fractions of physically available frequency-time units. Per-TTI summaries separate TDD-unavailable, available-but-empty, and assigned-with-zero-effective-payload units.

Payload/grant efficiency is sampled from logged grid grants and is the sum of effective payload bits divided by nominal grant bits.
