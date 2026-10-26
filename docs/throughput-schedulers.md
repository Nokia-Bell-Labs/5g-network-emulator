# Throughput Scheduler Family

## Scope

FikoRE exposes Max Throughput, Proportional Fair, Blind Equal Throughput, and Round Robin as independent configuration choices. Max Throughput, PF, and BET share one implementation of the instantaneous score, while Round Robin retains its cursor-based implementation and does not use throughput history. FIFO and delay schedulers are outside this family.

## Configuration aliases

| `metric_type` | Scheduler | Rate exponent | History exponent | History | Provisional reranking |
|---:|---|---:|---:|---|---|
| 1 | Blind Equal Throughput (BET) | 0 | 1 | TTI EWMA | Optional |
| 4 | Max Throughput (MT) | 1 | 0 | None | Never |
| 5 | Round Robin (RR) | n/a | n/a | None | Never |
| 6 | Proportional Fair (PF) | `pf_alpha` | 1 | TTI EWMA | Optional |

The shared score for UE \(i\) and allocation unit \(b\) is

\[
M_{i,b}=w_i\frac{r_{i,b}^{\alpha}}{\max(\bar R_i,\epsilon)^\beta},
\qquad \epsilon=10^{-6},
\]

where \(w_i\) is the UE priority, \(r_{i,b}\) is the current achievable rate, and \(\bar R_i\) is committed or provisionally projected service history. MT therefore maximizes \(w_i r_{i,b}\); PF balances current rate against past service; pure BET ranks only by inverse past service. RR does not evaluate this expression and ignores UE priority.

## History lifecycle

PF and BET use the same exponentially weighted service history:

\[
\bar R_i(t+1)=(1-a)\bar R_i(t)+a\,x_i(t),
\qquad a=1-\exp(-1\,\mathrm{ms}/\tau),
\]

where \(\tau\) is `throughput_time_window_ms` and \(x_i(t)\) is effective payload delivered by the radio grant during the active 1 ms TTI. The update runs once per active TTI and is independent of CQI reporting cadence. A backlogged eligible UE that receives no effective service contributes zero; an inactive or non-backlogged UE freezes its state. A newly active UE is initialized from its standalone achievable rate, and detaching it resets the state.

MT never initializes, reads, projects, commits, or resets throughput history. This capability gate avoids both a fairness side effect and unnecessary history work. RR remains outside the lifecycle.

## Intra-TTI reranking

`throughput_intra_tti_update` accepts `none` or `allocation_unit`. PF and BET may use either value. In `allocation_unit` mode, each nominal grant updates a provisional current-TTI service projection before the next RB or RBG is ranked; the committed EWMA is still updated only once at TTI end from effective payload. MT, RR, FIFO, and delay schedulers reject `allocation_unit` instead of silently ignoring it.

Grouped RBGs can otherwise let one initial winner collect several units before the committed history changes. Provisional reranking generally reduces short-window service gaps in homogeneous PF and BET cases, but its cost and benefit depend on the number of scheduling decisions, so both modes remain configurable.

## Tie handling and eligibility

All metric schedulers use the generic allocation-unit tie cursor for scores equal within \(10^{-6}\). Equal-rate MT UEs therefore rotate without acquiring a provisional fairness penalty. Candidate eligibility remains common: the UE must be enabled, have queued data, have a positive achievable rate, and have rate-cap credit when a cap is configured.

## Configuration examples

PF with grouped-grant reranking:

```ini
[MACLayer]
metric_type: 6
pf_alpha: 1.0
throughput_time_window_ms: 100.0
throughput_intra_tti_update: allocation_unit
```

Pure BET with the same history cadence:

```ini
[MACLayer]
metric_type: 1
throughput_time_window_ms: 100.0
throughput_intra_tti_update: allocation_unit
```

Max Throughput and Round Robin:

```ini
[MACLayer]
metric_type: 4
throughput_intra_tti_update: none
```

```ini
[MACLayer]
metric_type: 5
throughput_intra_tti_update: none
```

`pf_alpha` is accepted only for PF. The former feature-branch keys `pf_time_window_ms` and `pf_intra_tti_update` were removed without parser aliases. Per-UE `beta_metric` is rejected for PF and BET because it represented a different historical BET formula; UE `priority` remains the explicit per-UE weight. Historical results must remain associated with their original source revision because no runtime legacy scheduler mode is provided.

## Characterization

`tools/benchmark_scheduler_family.py` covers all four aliases across homogeneous and heterogeneous near/far channels, full-buffer and finite demand, grouped and per-PRB frequency grids, and localized and distributed time grids. The homogeneous arm places every UE outdoors at 200 m; the heterogeneous arm pairs 50 m outdoor UEs with 200 m low-loss-indoor UEs so both groups remain eligible while their achievable rates differ. The driver records aggregate throughput, Jain fairness, maximum effective-service gaps, decision count, and warmed-up per-TTI runtime quantiles. Scheduler regression tests separately cover formulas, epsilon behavior, priority, cold start, idle freeze, CQI-cadence independence, detach reset, MT strongest-user selection and equal-rate tie rotation, BET fairness, PF continuity, and RR priority independence.
