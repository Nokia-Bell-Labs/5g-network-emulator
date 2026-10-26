# PF Granularity Benchmark

Warm-up steps: 20; measured steps per sample: 100; repeats: 10.

P50/P95/P99/max are computed from individually timed warmed-up TTIs across all repeats. A compute-budget exceedance is a TTI above 1,000 us.

Host: `pitahaya`; platform: `Linux-5.15.0-58-generic-x86_64-with-glibc2.35`.

Time modes: `localized` uses one time allocation group per 1 ms; `distributed` uses one group per numerology slot.

Frequency modes: `grouped` uses configured RBGs; `per_rb` uses one PRB per allocation unit.

Offered traffic is approximately 4 Gbit/s in each direction per case, divided equally across UEs. This maintains backlog without the unbounded memory growth caused by a 100 Gbit/s target per UE.

| grid | ues | time_mode | frequency_mode | reranking | decisions_per_tti | us_per_tti_p50 | us_per_tti_p95 | us_per_tti_p99 | us_per_tti_max | compute_budget_exceedance_pct | dl_total_mbps | dl_jain | dl_max_service_gap_ttis |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| 20mhz_mu1 | 1 | localized | grouped | none | 6 | 34.385 | 42.159 | 48.103 | 86.873 | 0.0 | 66.291 | 1 | 1 |
| 20mhz_mu1 | 1 | localized | grouped | allocation_unit | 6 | 31.559 | 39.143 | 43.533 | 53.15 | 0.0 | 66.291 | 1 | 1 |
| 20mhz_mu1 | 1 | distributed | per_rb | none | 100 | 89.138 | 113.491 | 125.464 | 146.205 | 0.0 | 69.12 | 1 | 1 |
| 20mhz_mu1 | 1 | distributed | per_rb | allocation_unit | 100 | 85.31 | 110.677 | 125.039 | 139.702 | 0.0 | 69.12 | 1 | 1 |
| 20mhz_mu1 | 16 | localized | grouped | none | 6 | 49.358 | 65.909 | 73.357 | 102.332 | 0.0 | 66.291 | 0.98699 | 27 |
| 20mhz_mu1 | 16 | localized | grouped | allocation_unit | 6 | 50.761 | 68.27 | 79.127 | 105.047 | 0.0 | 66.291 | 0.99974 | 5 |
| 20mhz_mu1 | 16 | distributed | per_rb | none | 100 | 197.686 | 262.509 | 307.393 | 339.437 | 0.0 | 69.056 | 0.98664 | 26 |
| 20mhz_mu1 | 16 | distributed | per_rb | allocation_unit | 100 | 248.791 | 325.523 | 360.002 | 401.323 | 0.0 | 69.013 | 0.99988 | 2 |
| 20mhz_mu1 | 64 | localized | grouped | none | 6 | 112.977 | 173.614 | 187.717 | 205.336 | 0.0 | 66.547 | 0.93982 | 99 |
| 20mhz_mu1 | 64 | localized | grouped | allocation_unit | 6 | 112.827 | 170.179 | 191.928 | 223.128 | 0.0 | 66.291 | 0.9958 | 18 |
| 20mhz_mu1 | 64 | distributed | per_rb | none | 100 | 575.189 | 875.515 | 988.71 | 1216.68 | 0.7 | 69.024 | 0.91477 | 100 |
| 20mhz_mu1 | 64 | distributed | per_rb | allocation_unit | 100 | 672.688 | 961.79 | 1050.395 | 1170.37 | 3.1 | 69.056 | 0.99963 | 8 |
| 20mhz_mu1 | 256 | localized | grouped | none | 6 | 361.077 | 562.772 | 657.224 | 754.446 | 0.0 | 66.205 | 0.27381 | 100 |
| 20mhz_mu1 | 256 | localized | grouped | allocation_unit | 6 | 369.132 | 581.811 | 694.362 | 812.294 | 0.0 | 66.376 | 0.91861 | 69 |
| 20mhz_mu1 | 256 | distributed | per_rb | none | 100 | 2238.275 | 3484.989 | 3845.572 | 4119.74 | 100.0 | 69.046 | 0.27835 | 100 |
| 20mhz_mu1 | 256 | distributed | per_rb | allocation_unit | 100 | 2315.245 | 3573.641 | 4144.3 | 4478.99 | 100.0 | 69.088 | 0.99528 | 44 |
| 100mhz_mu1 | 1 | localized | grouped | none | 17 | 41.408 | 59.041 | 64.732 | 71.524 | 0.0 | 376.086 | 1 | 1 |
| 100mhz_mu1 | 1 | localized | grouped | allocation_unit | 17 | 36.669 | 47.549 | 53.04 | 93.024 | 0.0 | 376.086 | 1 | 1 |
| 100mhz_mu1 | 1 | distributed | per_rb | none | 546 | 619.242 | 749.762 | 795.207 | 845.717 | 0.0 | 377.004 | 1 | 1 |
| 100mhz_mu1 | 1 | distributed | per_rb | allocation_unit | 546 | 628.279 | 761.314 | 799.281 | 866.355 | 0.0 | 377.004 | 1 | 1 |
| 100mhz_mu1 | 16 | localized | grouped | none | 17 | 74.61 | 111.16 | 121.405 | 136.276 | 0.0 | 375.572 | 0.98803 | 27 |
| 100mhz_mu1 | 16 | localized | grouped | allocation_unit | 17 | 83.211 | 123.148 | 133.443 | 144.711 | 0.0 | 375.572 | 0.99989 | 3 |
| 100mhz_mu1 | 16 | distributed | per_rb | none | 546 | 1202.475 | 1601.6 | 1813.293 | 1954.79 | 93.4 | 377.101 | 0.97899 | 35 |
| 100mhz_mu1 | 16 | distributed | per_rb | allocation_unit | 546 | 1235.005 | 1675.879 | 1866.814 | 2140.95 | 86.1 | 376.887 | 0.99111 | 12 |
| 100mhz_mu1 | 64 | localized | grouped | none | 17 | 193.728 | 334.493 | 363.566 | 426.951 | 0.0 | 376.086 | 0.93717 | 100 |
| 100mhz_mu1 | 64 | localized | grouped | allocation_unit | 17 | 207.288 | 350.681 | 376.924 | 398.227 | 0.0 | 376.086 | 0.99962 | 11 |
| 100mhz_mu1 | 64 | distributed | per_rb | none | 546 | 3283.75 | 4793.207 | 5454.411 | 6411.39 | 100.0 | 376.897 | 0.89979 | 100 |
| 100mhz_mu1 | 64 | distributed | per_rb | allocation_unit | 546 | 3404.615 | 5185.564 | 5777.684 | 7988.68 | 100.0 | 377.058 | 0.96132 | 48 |
| 100mhz_mu1 | 256 | localized | grouped | none | 17 | 667.242 | 1222.435 | 1385.885 | 1556.14 | 17.2 | 373.038 | 0.29829 | 100 |
| 100mhz_mu1 | 256 | localized | grouped | allocation_unit | 17 | 746.083 | 1306.081 | 1500.48 | 1665.92 | 21.3 | 375.401 | 0.96741 | 38 |
| 100mhz_mu1 | 256 | distributed | per_rb | none | 546 | 11702.5 | 17222.58 | 19804.296 | 21812.9 | 100.0 | 377.07 | 0.3244 | 100 |
| 100mhz_mu1 | 256 | distributed | per_rb | allocation_unit | 546 | 11723.35 | 17657.625 | 19937.016 | 20922.5 | 100.0 | 377.133 | 0.91595 | 91 |
| 400mhz_mu3 | 1 | localized | grouped | none | 15 | 43.436 | 52.651 | 57.463 | 72.446 | 0.0 | 1249.01 | 1 | 0 |
| 400mhz_mu3 | 1 | localized | grouped | allocation_unit | 15 | 43.597 | 51.969 | 57.062 | 68.168 | 0.0 | 1249.01 | 1 | 0 |
| 400mhz_mu3 | 1 | distributed | per_rb | none | 2000 | 4667.165 | 6215.568 | 6737.591 | 7933.5 | 100.0 | 1233.48 | 1 | 0 |
| 400mhz_mu3 | 1 | distributed | per_rb | allocation_unit | 2000 | 4765.02 | 6282.474 | 6962.823 | 10166.7 | 100.0 | 1233.48 | 1 | 0 |
| 400mhz_mu3 | 16 | localized | grouped | none | 15 | 87.584 | 115.988 | 124.739 | 157.816 | 0.0 | 1246.57 | 0.99468 | 24 |
| 400mhz_mu3 | 16 | localized | grouped | allocation_unit | 15 | 105.077 | 133.675 | 143.548 | 191.619 | 0.0 | 1245.66 | 0.99983 | 4 |
| 400mhz_mu3 | 16 | distributed | per_rb | none | 2000 | 4506.105 | 5939.962 | 6466.237 | 7316.11 | 100.0 | 1232.82 | 0.98135 | 16 |
| 400mhz_mu3 | 16 | distributed | per_rb | allocation_unit | 2000 | 4298.68 | 5471.139 | 5976.299 | 6561.94 | 100.0 | 1232.89 | 0.99432 | 15 |
| 400mhz_mu3 | 64 | localized | grouped | none | 15 | 218.955 | 318.736 | 333.401 | 409.417 | 0.0 | 1242.7 | 0.92353 | 83 |
| 400mhz_mu3 | 64 | localized | grouped | allocation_unit | 15 | 246.522 | 353.573 | 378.916 | 393.417 | 0.0 | 1248.56 | 0.99375 | 18 |
| 400mhz_mu3 | 64 | distributed | per_rb | none | 2000 | 9699.265 | 11835.345 | 12361.889 | 13765.2 | 100.0 | 1233.21 | 0.97905 | 56 |
| 400mhz_mu3 | 64 | distributed | per_rb | allocation_unit | 2000 | 10133.6 | 12277.13 | 13020.716 | 14775.8 | 100.0 | 1233.15 | 0.97937 | 56 |
| 400mhz_mu3 | 256 | localized | grouped | none | 15 | 750.381 | 1206.213 | 1368.213 | 1632.05 | 20.4 | 1163.34 | 0.5199 | 100 |
| 400mhz_mu3 | 256 | localized | grouped | allocation_unit | 15 | 809.938 | 1330.161 | 1495.212 | 1826.17 | 22.5 | 1204.7 | 0.9859 | 66 |
| 400mhz_mu3 | 256 | distributed | per_rb | none | 2000 | 32285.55 | 39642.875 | 41507.197 | 47274.5 | 100.0 | 1232.77 | 0.49466 | 100 |
| 400mhz_mu3 | 256 | distributed | per_rb | allocation_unit | 2000 | 32711.0 | 39832.86 | 41838.918 | 46530.3 | 100.0 | 1232.75 | 0.54132 | 100 |

Failed cases: 0.
