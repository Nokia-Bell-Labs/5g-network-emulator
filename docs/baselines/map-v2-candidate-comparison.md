# V2 Candidate Map Comparison

## Scope

The v2 generator was used to create 21 deterministic candidate maps with
master seed `20260927`, including the new exact 2.38 GHz UMi map.

At the time of this controlled comparison, no production map had been
replaced. Comparison runs selected candidate maps through the explicit
`map_file` scenario setting.

Changes represented together in these candidates:

- corrected UMa UE-height LOS term;
- correlated binary LOS state;
- odd 291×291 grid with an explicit centre cell;
- deterministic Python DFT-based shadow realization;
- approved exact-frequency catalog.

ABG coefficient values are unchanged.

## Spatial map comparison

Twenty thousand common spatial samples were evaluated for each map that has a
legacy counterpart.

The complete results are in
[`map-v2-spatial-deltas.csv`](map-v2-spatial-deltas.csv).

General observations:

- median gain deltas are typically within approximately ±4 dB;
- realization-to-realization P5/P95 differences frequently exceed ±10 dB;
- open-office candidates show the largest positive upper-tail changes;
- UMi 3.5 has a median delta near -4 dB against its current legacy
  realization;
- pointwise RMSE includes both approved semantic changes and an independent
  stochastic realization, so it is not a regression tolerance.

## Canonical profile comparison

Both run batches use:

- identical code;
- seed `20260927`;
- identical traffic, mobility, power, scheduler, and duration;
- 180 simulated seconds per profile.

Only the selected map catalog changes.

| Profile | Dir. | Legacy throughput | V2 throughput | Delta | Legacy mean SINR | V2 mean SINR | SINR delta | Legacy outage UEs | V2 outage UEs |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|
| Indoor n78 | DL | 46.78 | 47.84 | +2.3% | 21.58 | 22.36 | +0.78 dB | 0 | 0 |
| Indoor n78 | UL | 51.14 | 47.62 | -6.9% | 31.66 | 32.53 | +0.87 dB | 0 | 0 |
| RMa n78 | DL | 50.81 | 62.25 | +22.5% | -4.46 | -1.36 | +3.10 dB | 2 | 0 |
| RMa n78 | UL | 25.08 | 27.49 | +9.6% | 2.45 | 5.00 | +2.55 dB | 0 | 0 |
| UMa n78 | DL | 112.92 | 108.73 | -3.7% | -0.99 | -1.82 | -0.83 dB | 5 | 5 |
| UMa n78 | UL | 14.36 | 14.31 | -0.4% | 7.01 | 6.17 | -0.84 dB | 2 | 1 |
| n258 FWA | DL | 499.98 | 499.98 | 0.0% | 60.48 | 60.27 | -0.21 dB | 0 | 0 |
| n258 FWA | UL | 156.59 | 156.53 | 0.0% | 59.75 | 59.56 | -0.20 dB | 0 | 0 |
| UMi n40 | DL | 48.19 | 48.29 | +0.2% | 27.01 | 27.96 | +0.95 dB | 0 | 0 |
| UMi n40 | UL | 24.74 | 24.19 | -2.2% | 36.49 | 37.48 | +0.99 dB | 0 | 0 |

Rates are Mbit/s.

The machine-readable profile results are in
[`map-v2-profile-deltas.csv`](map-v2-profile-deltas.csv).

## Interpretation

1. UMi, UMa, and indoor aggregate effects are moderate for this seed.
2. RMa changes materially: approximately +3 dB mean DL SINR, two fewer outage
   UEs, and +22.5% aggregate DL throughput.
3. The outdoor scalar-gain n258 profile is capacity-saturated and therefore
   insensitive to the map change in aggregate throughput.
4. One old/new realization comparison cannot establish expected coverage
   deltas. Multi-seed confidence intervals are required for that claim.
5. The RMa change warrants explicit owner review before candidate maps become
   production defaults.

## Activation decision

The owner approved option 2: activate all v2 maps, including RMa. Production
activation on 2026-09-28 preserved every reviewed numeric array and changed
only the metadata semantic version from pending to production.
