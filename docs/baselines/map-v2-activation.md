# Production V2 Map Activation Record

**Historical status:** This records the initial v2.0 activation. It was
superseded by v2.1 after adversarial validation found and corrected periodic
FFT edge seams.

## Decision

All 21 owner-reviewed v2 maps were activated on 2026-09-28, including the RMa
maps and the exact 2.38 GHz UMi map. The activation preserves the approved ABG
coefficient values and accepts the previously measured material RMa profile
delta.

## Reproduction

- Master seed: `20260927`
- Generator: `fikore-map-generator` version `2.0.0`
- Map semantic version: `2.0.0`
- Grid: 291×291 with an explicit centre cell
- Production catalog SHA-256:
  `3a1c020b1873aec0a06e7a443e8dac69cd2840067ebc3154392bc7547f75a6d2`
- Production manifest SHA-256:
  `f4e84042f2a9a760cc84023246814a568a1f5c0e9e4b29cee960b4ab198cfcd3`

Two clean temporary catalogs were generated independently. All 21 map files
and `CATALOG.json` were byte-identical between generations. The numeric arrays
of all production files were also compared against the reviewed candidate
files and were identical. Production bytes differ from candidate bytes only
because the embedded semantic version and generator version now identify a
production release.

## Runtime activation

`AVAILABLE_FREQUENCIES["URBAN_MICROCELL"]` now includes 2.38 GHz. The canonical
n40 profile therefore resolves to
`macroscopic_fading_map_URBAN_MICROCELL_2.38.json` without nearest-frequency
fallback.

The runtime continues to read explicitly selected legacy-v1 files for archived
experiments. No feature-branch compatibility alias is retained for the removed
`location` configuration key.

## Checks executed

```bash
python3 tools/maps/test_generator_v2.py
python3 tools/maps/build_manifest.py --check
python3 tools/maps/validate_maps.py
make test
make smoke
```

All checks passed after activation. Post-activation profile and multi-seed
statistics are recorded separately so this record remains a byte-provenance
artifact rather than a performance claim.
