# FikoRE Macroscopic Map Schema

## Legacy v1

Historical map files contain:

```json
{
  "cell_number": 290,
  "cell_size": 5.0,
  "map": [[-80.0]]
}
```

- `cell_number`: number of cells per side.
- `cell_size`: cell spacing in metres.
- `map`: square matrix of macroscopic link gain in dB, combining the selected
  path-loss and shadow-fading realization.

The v1 format does not identify scenario, carrier frequency, model
coefficients, generator version, RNG, seed, or coordinate-origin convention.
Those properties are inferred from filenames and historical context.

The runtime retains v1 reading so archived studies can select historical maps
explicitly. Production maps use v2.

## Production v2 metadata

The v2 schema is the production contract for deterministic generation.

```json
{
  "schema_version": 2,
  "metadata": {
    "scenario": "URBAN_MICROCELL",
    "frequency_ghz": 2.38,
    "seed": 42,
    "cell_number": 291,
    "cell_size_m": 5.0,
    "ue_height_m": 1.5,
    "realization_id": "urban_microcell-2.38-seed-42-parameterhash",
    "generator": {
      "name": "fikore-map-generator",
      "version": "2.1.0"
    },
    "grid_origin": "explicit-center-cell",
    "pathloss_family": "ABG",
    "semantic_version": "2.1.0",
    "los_abg": {"alpha": 2.27, "beta": 27.02, "gamma": 2.0},
    "nlos_abg": {"alpha": 2.8, "beta": 31.4, "gamma": 2.7},
    "los_shadow_sigma_db": 4.3,
    "nlos_shadow_sigma_db": 6.8,
    "los_decorrelation_m": 10.0,
    "nlos_decorrelation_m": 13.0,
    "los_state_model": "correlated-gaussian-cdf-threshold",
    "los_probability_model": "3gpp-tr-38.901-v18.1.0-clause-7.4.2-umi",
    "coefficient_validity_range": "not-encoded"
  },
  "cell_number": 291,
  "cell_size": 5.0,
  "map": [[-80.0]]
}
```

## Compatibility policy

- The runtime must continue to read v1 files during migration.
- V2 metadata does not change the interpretation of the numeric map array
  without a new approved semantic version.
- Canonical profiles use exact scenario-frequency lookup.
- Nearest-frequency fallback is disabled by default. It requires
  `allow_nearest_map_fallback: true`, and the runtime reports both requested
  and selected frequencies.
- A map realization is immutable: changed bytes require a new realization ID
  and checksum.

## Validation requirements

For every generated realization:

1. Matrix dimensions and finite values match metadata.
2. Same generator, parameters, and seed produce identical bytes.
3. Different seeds preserve approved distribution tolerances.
4. LOS/NLOS path-loss slices match approved ABG equations.
5. Shadow mean, standard deviation, and radial autocorrelation match targets.
6. LOS probability versus distance matches the approved LOS-state algorithm.
7. Runtime interpolation and generator origin conventions agree.
8. Input and output hashes are recorded.

LOS selection, UMa LOS correction, origin, the preserved coefficient table,
and O2I ownership were approved in `map-o2i-design-review.md`. Their external
validity and deployment-specific calibration remain explicitly limited.
