# PF Granularity Benchmark

Warm-up steps: 500; measured steps per sample: 2000; repeats: 1.

P50/P95/P99/max are computed from individually timed warmed-up TTIs across all repeats. A compute-budget exceedance is a TTI above 1,000 us.

Host: `pitahaya`; platform: `Linux-5.15.0-58-generic-x86_64-with-glibc2.35`.

Time modes: `localized` uses one time allocation group per 1 ms; `distributed` uses one group per numerology slot.

Frequency modes: `grouped` uses configured RBGs; `per_rb` uses one PRB per allocation unit.

Offered traffic is approximately 4 Gbit/s in each direction per case, divided equally across UEs. This maintains backlog without the unbounded memory growth caused by a 100 Gbit/s target per UE.

| grid | ues | time_mode | frequency_mode | reranking | decisions_per_tti | us_per_tti_p50 | us_per_tti_p95 | us_per_tti_p99 | us_per_tti_max | compute_budget_exceedance_pct | dl_total_mbps | dl_jain | dl_max_service_gap_ttis |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 20mhz_mu1 | 64 | localized | grouped | none | 6 | 104.34 | 156.678 | 173.557 | 385.863 | 0.0 | 66.758 | 0.99899 | 96 |
| 20mhz_mu1 | 64 | localized | grouped | allocation_unit | 6 | 109.25 | 162.998 | 181.234 | 378.068 | 0.0 | 66.731 | 0.99988 | 18 |
| 20mhz_mu1 | 64 | distributed | per_rb | none | 100 | 548.673 | 811.603 | 936.607 | 1133.31 | 0.05 | 69.514 | 0.99899 | 96 |
| 20mhz_mu1 | 64 | distributed | per_rb | allocation_unit | 100 | 560.235 | 830.141 | 971.698 | 1138.68 | 0.55 | 69.514 | 1 | 2 |
| 100mhz_mu1 | 64 | localized | grouped | none | 17 | 186.274 | 321.159 | 346.534 | 497.262 | 0.0 | 378.18 | 0.999 | 96 |
| 100mhz_mu1 | 64 | localized | grouped | allocation_unit | 17 | 209.678 | 350.033 | 378.584 | 641.302 | 0.0 | 378.194 | 1.0 | 6 |
| 100mhz_mu1 | 64 | distributed | per_rb | none | 546 | 3302.995 | 4727.023 | 5297.833 | 6085.66 | 100.0 | 379.551 | 0.999 | 96 |
| 100mhz_mu1 | 64 | distributed | per_rb | allocation_unit | 546 | 4145.94 | 5966.954 | 6778.134 | 7355.36 | 100.0 | 379.542 | 1 | 1 |
| 400mhz_mu3 | 64 | localized | grouped | none | 15 | 219.727 | 322.694 | 354.246 | 509.295 | 0.0 | 1245.61 | 0.99938 | 81 |
| 400mhz_mu3 | 64 | localized | grouped | allocation_unit | 15 | 249.172 | 354.947 | 374.343 | 516.778 | 0.0 | 1245.51 | 1.0 | 6 |
| 400mhz_mu3 | 64 | distributed | per_rb | none | 2000 | 11006.0 | 12816.27 | 13492.695 | 16929.3 | 100.0 | 1232.42 | 0.99965 | 83 |
| 400mhz_mu3 | 64 | distributed | per_rb | allocation_unit | 2000 | 11978.75 | 14133.68 | 14954.933 | 16559.5 | 100.0 | 1232.43 | 1 | 0 |

Failed cases: 0.
