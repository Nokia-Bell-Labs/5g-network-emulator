# PF Intra-TTI Granularity Benchmark

## Purpose

This benchmark evaluates the experimental
`throughput_intra_tti_update: allocation_unit` mode against `none` across the two time
aggregation modes and the two frequency aggregation modes.

The PF EWMA is committed once per 1 ms TTI in both modes. Allocation-unit mode
uses nominal scheduled bits only to update provisional ordering within the
current TTI.

## Method

- Static homogeneous UEs at 200 m.
- Full-buffer UL and DL traffic.
- No stochastic fading or external interference.
- PF alpha 1 and 100 ms EWMA.
- Single-threaded emulator.
- 50 simulated TTIs per case.
- One timing repetition in the initial screening run.
- Logs disabled.

Grids:

- 20 MHz / μ1;
- 100 MHz / μ1;
- 400 MHz / μ3.

UE populations: 16 and 64.

Aggregation:

- localized + grouped;
- distributed + grouped;
- localized + per-RB;
- distributed + per-RB.

The complete machine-readable results are in
[`pf-granularity-benchmark.csv`](pf-granularity-benchmark.csv).

## Aggregate findings

| Frequency mode | Median runtime change | Runtime range | Mean DL Jain gain | Mean maximum-gap reduction |
|---|---:|---:|---:|---:|
| Grouped RBG | +1.3% | -14.0% to +13.5% | +0.261 | 27.5 TTIs |
| Per-RB | +1.5% | -3.2% to +5.9% | +0.119 | 12.4 TTIs |

The negative timing values in some short cases are measurement noise; the
screening run is sufficient to distinguish negligible grouped-mode overhead
from potentially large high-resolution overhead, but not to establish a stable
performance regression threshold.

## Representative results

### 100 MHz / μ1 / 64 UEs

| Time mode | Frequency mode | Decisions/TTI | Reranking | µs/TTI | DL Jain | Max DL gap |
|---|---|---:|---|---:|---:|---:|
| Localized | Grouped | 17 | none | 35,448 | 0.450 | 47 |
| Localized | Grouped | 17 | allocation unit | 35,529 | 0.997 | 13 |
| Distributed | Grouped | 34 | none | 35,745 | 0.449 | 47 |
| Distributed | Grouped | 34 | allocation unit | 35,993 | 0.998 | 9 |
| Localized | Per-RB | 273 | none | 37,897 | 0.450 | 47 |
| Localized | Per-RB | 273 | allocation unit | 38,609 | 0.569 | 41 |
| Distributed | Per-RB | 546 | none | 38,723 | 0.447 | 47 |
| Distributed | Per-RB | 546 | allocation unit | 38,621 | 0.539 | 35 |

### 400 MHz / μ3 / 64 UEs

| Time mode | Frequency mode | Decisions/TTI | Reranking | µs/TTI | DL Jain | Max DL gap |
|---|---|---:|---|---:|---:|---:|
| Localized | Grouped | 15 | none | 35,419 | 0.625 | 47 |
| Localized | Grouped | 15 | allocation unit | 35,898 | 0.994 | 19 |
| Distributed | Grouped | 120 | none | 36,061 | 0.625 | 47 |
| Distributed | Grouped | 120 | allocation unit | 36,694 | 0.991 | 13 |
| Localized | Per-RB | 250 | none | 38,200 | 0.625 | 47 |
| Localized | Per-RB | 250 | allocation unit | 40,466 | 0.625 | 47 |
| Distributed | Per-RB | 2,000 | none | 47,359 | 0.476 | 47 |
| Distributed | Per-RB | 2,000 | allocation unit | 46,852 | 0.476 | 47 |

## Interpretation

1. Allocation-unit reranking provides a large fairness and continuity benefit
   in grouped-RBG modes.
2. After the scalar-denominator optimization, both grouped and per-RB median
   runtime effects are close to the noise floor of this short benchmark.
3. Fine per-RB grants may be too small to materially change a 100 ms PF EWMA
   within one TTI; the 400 MHz / μ3 per-RB cases show no fairness benefit.
4. At very high decision counts, fairness after only 50 TTIs remains sensitive
   to TDD phase and finite observation duration; longer functional runs are
   required before comparing final Jain values.
5. Aggregate throughput changes little in most DL cases; the primary benefit is
   service continuity and fairness.

## Production policy

- Keep both modes configurable.
- Use `allocation_unit` in the canonical grouped-RBG PF profiles.
- Keep `none` as the conservative default for per-RB high-resolution
  experiments until a longer repeated benchmark establishes the acceptable
  runtime envelope.
- Run a repeated timing suite on the target real-time host before changing the
  global parser default.

This policy was approved on 2026-09-25. The results above use the optimized
O(1) scalar-denominator update.
