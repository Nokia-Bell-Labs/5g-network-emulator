# Macroscopic Map Provenance

## Current production set

FikoRE ships 21 deterministic v2 map realizations under
`include/maps_scenarios/`. The catalog includes an exact 2.38 GHz UMi map.
`CATALOG.json` records the master seed (`20260927`) and derived realization
seeds; `MANIFEST.json` records embedded metadata, statistics, and byte hashes.

Production v2 properties:

- maintained Python generator version `2.1.0`;
- map semantic version `2.1.0`;
- odd 291×291 grids with one explicit centre cell;
- correlated binary LOS state;
- corrected UMa UE-height LOS term;
- padded FFT embedding and center cropping to remove periodic edge seams;
- unchanged approved ABG coefficient values;
- deterministic compact JSON serialization.

Version 2.0 initially promoted the owner-approved candidate arrays. Adversarial
validation then found a periodic opposite-edge seam caused by same-size FFT
filtering. The owner approved padded regeneration; version 2.1 therefore
changes all numeric arrays while preserving coefficients, seeds, dimensions,
and the other approved semantics.

## Historical lineage

The design archive is stored outside this repository under
`/home/pablop/devel/chema`.

Relevant snapshots:

1. `NOKIA/MAP_GENERATION/`: original MATLAB scripts and output maps;
2. `5g-diego/maps_scenarios/`: emulator handoff snapshot;
3. `old/5g-network-emulator/include/maps_scenarios/`: earlier frequency set;
4. `shadow_fading_maps/`: experimental Python port;
5. current `tools/matlab/shadow_fading_map/`: documented MATLAB refactor.

The raw design archive and measurement datasets are not vendored here.

## Known divergent files

| Map | Original MATLAB hash prefix | Handoff hash prefix | Legacy-v1 hash prefix |
|---|---|---|---|
| `RURAL_MACROCELL_3.5` | `468207af4f3a` | `b18f7b896d7b` | `3d89a1fa6d94` |
| `URBAN_MICROCELL_3.5` | `f5c3369d8c7d` | `f5c3369d8c7d` | `705e41914989` |

An older UMi 2.38 GHz realization exists with SHA-256
`3ff2eb544e2efe4a0add85ada5b20316431faa9914e85f456ecd63f8c8891995`.
It must not be restored as a production map until its generator semantics and
validation are approved.

## Legacy generation model

The archived generator:

1. builds LOS and NLOS ABG path-loss maps;
2. generates independent Gaussian shadow fields;
3. filters shadow fields using a DFT-derived spatial filter;
4. draws a LOS state from the scenario probability;
5. smooths the LOS state spatially;
6. combines LOS/NLOS macroscopic fields;
7. exports a 290×290 JSON matrix.

The deterministic batch wrapper in this repository sets an explicit MATLAB
RNG seed and preserves these legacy operations. It is a reproduction tool, not
an approval of the current LOS-mask or origin semantics.

## Approved v2 semantics

The signed design review in `map-o2i-design-review.md` approves:

- configured UE height in the UMa LOS-probability correction;
- correlated binary LOS selection instead of a floating smoothed mask;
- an odd grid with an explicit centre-cell origin;
- Python as the maintained generator;
- preservation of the current ABG coefficient values;
- exact 2.38 GHz lookup for the canonical n40 profile;
- exact lookup by default, with explicit opt-in for nearest-frequency fallback;
- one shipped deterministic realization per scenario-frequency pair;
- separate runtime ownership of building and vehicle penetration.

The shopping-mall LOS probability is retained as a legacy FikoRE heuristic;
TR 38.901 does not define that fixed shopping-mall expression. Its provenance
remains pending and is labelled in map metadata.

Source-fit distance ranges and site-specific calibration remain limitations,
not inferred properties of a large generated square. Rayleigh-only
small-scale fading also remains an explicit Phase 3 limitation.

## Reproduction policy

Every future map must have:

- deterministic seed and named RNG;
- generator source commit/version;
- complete parameter metadata;
- immutable realization ID;
- input/output hashes;
- statistical validation report;
- explicit approval before replacing a shipped realization.

Production regeneration command:

```bash
python3 tools/maps/generate_v2_catalog.py \
  --master-seed 20260927 \
  --output include/maps_scenarios
python3 tools/maps/build_manifest.py
python3 tools/maps/validate_maps.py
```
