# PHY Model V2 Deterministic Baseline

## Provenance

- Base code SHA: `e995563fd3e8fa300f4b0accca3b494f7177101c`
- Feature branch paper commit: `1c04acc`
- Run seed: `20260924`
- Simulated duration per profile: 180 seconds
- Batch ID: `phy-v2-baseline-e995563-seed20260924`
- Run date: 2026-09-24

No runtime behavior or profile value changed between the base SHA and this
baseline. The only preceding feature-branch change is the external-review
paper.

## Build and test baseline

Commands:

```bash
make -j4
make test
make smoke
```

Results:

- C++ unit tests: 7/7 passed.
- Dashboard logic tests: 11/11 passed.
- Smoke simulation: passed.
- Runtime-control smoke simulation: passed.

## Offline reference runs

All runs completed with return code zero:

| Profile | Scheduler | UEs | Wall time |
|---|---|---:|---:|
| UMi n40 NPN | PF | 11 | 6.26 s |
| UMa n78 pedestrian | PF | 21 | 9.51 s |
| RMa n78 vehicular | RR | 11 | 233.29 s |
| Indoor n78 pedestrian | PF | 11 | 7.25 s |
| UMi 26 GHz FWA | PF | 11 | 15.27 s |

The RMa run is substantially slower because its distributed, per-RB grid
performs approximately 1,000 allocation decisions per simulated millisecond.

## Aggregate radio and traffic metrics

| Profile | Dir. | Offered | Delivered | Errors | Mean demand satisfaction | PHY outage UEs | Non-outage UEs below 10% demand | Maximum non-outage zero-service gap |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| Indoor n78 | DL | 65.00 | 61.16 | 3.84 | 99.4% | 0 | 0 | 0 ms |
| Indoor n78 | UL | 90.00 | 90.00 | 0.00 | 100.0% | 0 | 0 | 0 ms |
| RMa n78 | DL | 120.00 | 82.64 | 37.30 | 66.0% | 0 | 0 | 32,120 ms |
| RMa n78 | UL | 120.00 | 97.11 | 22.87 | 79.2% | 0 | 0 | 13,380 ms |
| UMa n78 | DL | 180.00 | 60.29 | 119.47 | 53.3% | 3 | 4 | 180,000 ms |
| UMa n78 | UL | 70.00 | 32.32 | 37.61 | 70.2% | 2 | 1 | 180,000 ms |
| UMi 26 GHz FWA | DL | 500.01 | 51.45 | 447.81 | 46.8% | 5 | 0 | 10 ms |
| UMi 26 GHz FWA | UL | 200.00 | 40.01 | 159.72 | 36.4% | 6 | 1 | 1,670 ms |
| UMi n40 | DL | 50.00 | 14.93 | 35.01 | 65.9% | 0 | 1 | 124,270 ms |
| UMi n40 | UL | 35.00 | 18.19 | 16.77 | 80.8% | 0 | 1 | 130,310 ms |

Rates are in Mbit/s. A PHY-outage UE has MCS below zero in at least 99% of
logged channel samples. Service-gap values are measured at the UE log sampling
resolution and conditioned on not being classified as permanent PHY outage.

The machine-readable source is
[`phy-model-v2-baseline.csv`](phy-model-v2-baseline.csv).

## Key characterization findings

1. Indoor n78 has no PHY-outage UEs and satisfies almost all offered traffic.
2. The 26 GHz FWA study UE remains in outage; active background UEs account
   for the delivered aggregate traffic.
3. The PF n40 and UMa profiles contain non-outage UEs with extremely long
   zero-service gaps, consistent with mixed per-UE PF exponents and CQI-gated
   metric state.
4. RMa mobility prevents permanent static outage but still produces
   multi-second service gaps and substantial error/expiry rates.
5. Payload/grant efficiency ranges from approximately 20% in low-load n40 DL
   to approximately 99% in saturated RMa.

## Comparison reference

The companion theoretical analysis reports:

- aggregate throughput correlation with this FikoRE baseline: 0.91;
- mean absolute aggregate throughput error: 18.3%;
- mean absolute payload/grant-efficiency difference: 3.6 percentage points.

The static model is therefore an order-of-magnitude and MAC diagnostic
reference, not a mobility, HARQ, or packet-expiry parity oracle.

## Reproduction notes

The five INIs were copied without behavioral changes and augmented only with:

```ini
seed: 20260924
run_id: <batch-id>-<profile-id>
```

Raw logs and rendered input copies are intentionally ignored by Git. The
analysis retained generated/offered throughput, delivered throughput, errors,
SINR/MCS summaries, outage classification, service gaps, sampled grant
efficiency, and wall runtime.

Future block-level reports must use the same seed and metric definitions, and
must explicitly document profile/configuration changes before comparing
deltas.
