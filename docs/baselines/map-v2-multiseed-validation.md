# V2 Map Multi-Seed Validation

## Method

- Independent master seeds: 30
- Master-seed sequence: `20270000` through `20270029`
- Catalog entries per seed: 21
- Runtime: 99.56 seconds
- Confidence intervals: two-sided 95% intervals over independent realizations; intervals are pointwise, not simultaneous, and spatial cells are not treated as replicates.
- Coverage proxy: fraction of map cells with map-only, pre-interference full-channel DL SNR >= 0 dB.
- Radial LOS bins cover the inscribed disk out to the map apothem; corner cells are excluded from radial diagnostics.

## Per-map summary

| Scenario | GHz | Link gain P50 (dB) | LOS radial RMSE | LOS shadow std (dB) | NLOS shadow std (dB) | LOS/NLOS correlation observed/target | Coverage proxy |
|---|---:|---:|---:|---:|---:|---:|---:|
| INDOOR_MIXED_OFFICE | 3.5 | -131.11 [-131.13, -131.08] | 0.017 | 4.30 | 7.90 | 0.407/0.407; 0.367/0.368 | n/a |
| INDOOR_MIXED_OFFICE | 26.0 | -146.53 [-146.55, -146.51] | 0.023 | 4.30 | 7.90 | 0.405/0.407; 0.368/0.368 | n/a |
| INDOOR_MIXED_OFFICE | 28.0 | -147.22 [-147.25, -147.19] | 0.026 | 4.30 | 7.90 | 0.405/0.407; 0.367/0.368 | n/a |
| INDOOR_OPEN_OFFICE | 3.5 | -129.29 [-129.33, -129.25] | 0.041 | 4.30 | 7.90 | 0.406/0.407; 0.366/0.368 | 26.4% |
| INDOOR_OPEN_OFFICE | 26.0 | -144.74 [-144.77, -144.71] | 0.044 | 4.30 | 7.90 | 0.406/0.407; 0.368/0.368 | n/a |
| INDOOR_OPEN_OFFICE | 28.0 | -145.40 [-145.44, -145.36] | 0.040 | 4.30 | 7.90 | 0.409/0.407; 0.368/0.368 | n/a |
| INDOOR_SHOPPING_MALL | 3.5 | -101.12 [-101.14, -101.11] | 0.014 | 3.30 | 4.60 | 0.366/0.368; 0.369/0.368 | n/a |
| INDOOR_SHOPPING_MALL | 26.0 | -121.13 [-121.14, -121.12] | 0.010 | 3.30 | 4.60 | 0.368/0.368; 0.369/0.368 | n/a |
| INDOOR_SHOPPING_MALL | 60.0 | -129.47 [-129.49, -129.46] | 0.016 | 3.30 | 4.60 | 0.369/0.368; 0.368/0.368 | n/a |
| RURAL_MACROCELL | 0.7 | -118.53 [-118.57, -118.49] | 0.023 | 1.70 | 6.70 | 0.368/0.368; 0.390/0.397 | n/a |
| RURAL_MACROCELL | 0.8 | -119.71 [-119.77, -119.66] | 0.022 | 1.70 | 6.70 | 0.369/0.368; 0.395/0.397 | n/a |
| RURAL_MACROCELL | 3.5 | -132.56 [-132.62, -132.50] | 0.024 | 1.70 | 6.70 | 0.368/0.368; 0.397/0.397 | 78.5% |
| URBAN_MACROCELL | 3.5 | -142.27 [-142.30, -142.25] | 0.013 | 2.40 | 5.30 | 0.367/0.368; 0.330/0.330 | 38.2% |
| URBAN_MACROCELL | 4.9 | -145.77 [-145.81, -145.74] | 0.014 | 2.40 | 5.30 | 0.366/0.368; 0.328/0.330 | n/a |
| URBAN_MACROCELL | 26.0 | -163.16 [-163.18, -163.14] | 0.011 | 2.40 | 5.30 | 0.367/0.368; 0.331/0.330 | n/a |
| URBAN_MACROCELL | 28.0 | -163.94 [-163.97, -163.92] | 0.013 | 2.40 | 5.30 | 0.368/0.368; 0.331/0.330 | n/a |
| URBAN_MICROCELL | 2.38 | -117.56 [-117.58, -117.53] | 0.021 | 4.30 | 6.80 | 0.368/0.368; 0.314/0.315 | 100.0% |
| URBAN_MICROCELL | 3.5 | -122.10 [-122.13, -122.08] | 0.023 | 4.30 | 6.80 | 0.367/0.368; 0.316/0.315 | n/a |
| URBAN_MICROCELL | 4.9 | -126.03 [-126.05, -126.01] | 0.021 | 4.30 | 6.80 | 0.369/0.368; 0.315/0.315 | n/a |
| URBAN_MICROCELL | 26.0 | -145.59 [-145.61, -145.57] | 0.022 | 4.30 | 6.80 | 0.368/0.368; 0.317/0.315 | 98.5% |
| URBAN_MICROCELL | 28.0 | -146.47 [-146.49, -146.45] | 0.019 | 4.30 | 6.80 | 0.367/0.368; 0.316/0.315 | n/a |

## Checks and interpretation

- Maximum absolute bias of the ensemble-mean radial LOS probability: 0.038.
- Maximum absolute ensemble shadow-standard-deviation error: 0.000 dB.
- Maximum absolute error of the ensemble-mean axial autocorrelation at the nearest grid-representable lag: 0.007.
- Maximum absolute ensemble-mean opposite-edge shadow correlation: 0.026; this diagnoses circular FFT seams and is not an acceptance pass.
- Autocorrelation targets use `exp(-lag/d_cor)` at the reported integer-cell lag; they are not incorrectly compared with `exp(-1)` when the declared distance falls between cells.
- Link-gain intervals quantify realization variability for the generator. They are not confidence intervals for field prediction error.
- The proxy excludes penetration, small-scale fading, external interference, mobility, MIMO, scheduling, HARQ, and traffic.
