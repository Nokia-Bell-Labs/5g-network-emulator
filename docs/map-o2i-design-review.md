# PHY Model V2 Map and O2I Design Review

**Status:** Approved on 2026-09-27
**Scope:** Block 2B semantic decisions  
**Activation:** All eleven semantic items and the 21-map production catalog
were approved; catalog activation occurred on 2026-09-28.

## 1. Preserved decisions

- ABG remains the path-loss family.
- Existing coefficient sets remain unchanged unless separately approved.
- Macroscopic maps remain the source of spatially coherent basic path loss and
  shadow fading.
- UL and DL use the same macroscopic realization for one UE position.
- Runtime O2I remains separate from the outdoor/basic path-loss map unless an
  approved profile explicitly states otherwise.

## 2. UMa LOS correction

### Current behavior

The legacy MATLAB function applies the UMa correction term using a fixed
`eNB_h = 25` value where the TR 38.901 LOS-probability expression uses UE
height.

### Proposed decision

- Use configured UE height \(h_{UT}\).
- Keep the base UMa LOS-probability equation from TR 38.901.
- Add radial probability tests at representative distances and heights.

**Recommendation:** Approve.

## 3. Spatial LOS-state generation

### Current behavior

1. Draw independent uniform thresholds.
2. Compare each threshold with radial LOS probability, producing a binary map.
3. Apply Gaussian filtering, producing floating-point values.
4. Combine fields with:

```matlab
los_mask .* macroscopic_los + (~los_mask) .* macroscopic_nlos
```

Logical negation of a nonzero floating-point value is zero, so the expression
is neither binary selection nor a convex LOS/NLOS blend.

### Options

#### A. Correlated binary LOS state

1. Generate a spatially correlated Gaussian field.
2. Convert it to a correlated uniform field through the Gaussian CDF.
3. Mark LOS where the field is below the radial LOS probability.
4. Select exactly one LOS or NLOS macroscopic field per cell.

Properties:

- binary state;
- preserves radial marginal probability approximately;
- controllable spatial correlation;
- physically clear interpretation.

#### B. Probabilistic LOS/NLOS blend

\[
M=p_{\mathrm{LOS}}M_{\mathrm{LOS}}
 +(1-p_{\mathrm{LOS}})M_{\mathrm{NLOS}}
\]

Properties:

- smooth expected link gain;
- no discrete LOS/NLOS regions;
- unsuitable if the map is intended as one physical realization;
- may suppress distribution tails.

#### C. Threshold the smoothed legacy mask

Properties:

- smallest implementation change;
- threshold and smoothing distort the intended radial LOS probability;
- behavior depends on kernel and threshold.

**Recommendation:** Option A. Use Option B only for an explicitly named
expected-loss map, not for a stochastic realization.

## 4. Grid origin and dimensions

### Current behavior

- Legacy generation uses an even 290×290 matrix and a MATLAB 1-based centre.
- Runtime interpolation maps `(0,0)` through a 0-based `width/2` index.
- The conventions differ by one cell.

### Options

1. Keep 290 cells and define the gNB at the intersection of the four central
   cells.
2. Move to an odd 291×291 grid with one explicit centre cell.

**Recommendation:** Use an odd 291×291 v2 grid and define
`origin_index = (cell_number - 1) / 2`. Keep v1 interpolation unchanged for
legacy maps.

## 5. Maintained generator

### Options

- MATLAB as production source, Python as validator/port.
- Python as production source, MATLAB as archived design oracle.

### Recommendation

Use Python as the maintained v2 generator because it supports:

- CI without a proprietary runtime;
- deterministic tests;
- manifest/schema integration;
- batch generation;
- reviewable text source.

Before replacing production maps, require numerical cross-checks against the
approved MATLAB equations and parameter tables. Preserve the deterministic
legacy MATLAB wrapper for historical reproduction.

## 6. ABG parameter catalog

For every scenario/frequency regime, approve and record:

- LOS and NLOS \(\alpha,\beta,\gamma,\sigma\);
- source publication;
- measurement frequency/distance range;
- allowed extrapolation range;
- validity warnings.

### Proposed decision

- Preserve the current coefficient values.
- Add explicit source/range metadata.
- Reject or warn when a requested scenario lies outside the approved range.
- Do not silently substitute a coefficient family from another scenario.

## 7. Frequency selection

### Current behavior

The nearest available map is selected. The n40 2.38 GHz profile therefore uses
the UMi 3.5 GHz realization.

### Proposed decision

- Generate an approved exact 2.38 GHz UMi realization.
- Require exact scenario/frequency matches for canonical/reference profiles.
- Allow nearest-frequency fallback only behind an explicit configuration
  option and emit requested/selected metadata.

**Recommendation:** Approve.

## 8. Realization policy

### Options

1. Ship one canonical realization per scenario/frequency.
2. Ship several numbered realizations.
3. Generate maps on demand and cache them by parameter/seed hash.

### Recommendation

- Ship one deterministic canonical realization for examples and regression.
- Support on-demand generation for study campaigns.
- Require multi-seed analysis for coverage claims.
- Do not commit large realization sets by default.

## 9. Map dimensions and scenario ranges

The v2 catalog must state the valid distance range from the source ABG model
and the intended scenario geometry.

### Proposed policy

- Map extent must not imply that the underlying coefficient fit is valid
  outside its source range.
- Mobility configuration must warn when it exceeds the approved map/model
  radius.
- Indoor-office maps should reflect an approved indoor layout/range rather
  than a generic large square.

Exact dimensions remain pending profile-by-profile review.

## 10. O2I ownership

### Proposed separation

- Maps represent the approved basic outdoor or indoor-scenario path-loss and
  shadow realization.
- Runtime UE state owns facade, indoor-depth, and vehicle penetration.
- A profile must state whether both endpoints are indoor; InH/InF then apply
  no exterior wall.
- Runtime must prevent double counting between map metadata and UE O2I state.

**Recommendation:** Approve.

## 11. Building penetration

### Proposed configuration

Replace the overloaded integer with explicit values:

```text
ue_location_type: outdoor | indoor | vehicle
building_penetration: none | low_loss | high_loss
```

For UMi/UMa:

- low-loss and high-loss use approved TR 38.901 formulas;
- penetration variation is drawn once per UE/session from a deterministic
  stream;
- indoor depth is UE-specific and spatially stable.

For InH/InF:

- `building_penetration: none` when gNB and UE are in the same indoor
  environment.

The legacy integer may be accepted only during a documented migration period.

## 12. Vehicle penetration

### Current mismatch

The TFM design specifies approximately \(N(9,5^2)\) dB, with a higher-loss
metallized alternative, while runtime code uses a fixed approximation and
indoor-depth term.

### Proposed decision

- Standard vehicle: draw \(N(9,5^2)\) dB once per UE/session.
- Metallized vehicle: configurable mean near 20 dB with approved variance.
- Do not add building indoor-depth loss to a vehicle.
- Keep the realization stable until the UE environment type changes.

**Recommendation:** Approve.

## 13. Small-scale fading

### Options

1. Keep Rayleigh for every link.
2. Add Rician LOS with configurable/scenario-derived K-factor.
3. Keep current behavior in Block 2 and defer the complete decision to the
   MIMO/beamforming consultation.

### Recommendation

Choose Option 3 for this branch. Document Rayleigh-only behavior and prepare a
separate Phase 3 design covering K-factor, beamforming, MIMO rank, blockage,
and update cadence.

## 14. Validation before map replacement

For old and proposed realizations, produce:

- radial LOS probability;
- LOS/NLOS path-loss slices;
- shadow mean/std/autocorrelation;
- map-value histograms and coverage CDFs;
- canonical UE SINR/MCS/outage deltas;
- five-profile throughput/error deltas;
- multiple-seed confidence intervals;
- interpolation/origin regression tests.

No map replacement is accepted solely because it passes structural tests.

## 15. Requested approval

The owner should approve or revise:

1. UMa LOS height correction.
2. Correlated binary LOS state (Option A).
3. Odd 291×291 v2 grid.
4. Python as maintained v2 generator.
5. Preserve ABG coefficients with source/range metadata.
6. Exact-frequency canonical maps and explicit fallback.
7. One shipped canonical seed plus on-demand study realizations.
8. Runtime ownership of O2I.
9. Explicit building penetration fields.
10. Gaussian vehicle-loss model.
11. Defer Rician/LOS small-scale changes to Phase 3.

All eleven design items were approved on 2026-09-27. The owner subsequently
approved activation of all 21 deterministic v2 maps, including the material RMa
change and exact 2.38 GHz UMi lookup. The promoted numeric arrays are identical
to the reviewed v2.0 candidate arrays.

An adversarial-review amendment on 2026-09-28 found that same-size FFT
filtering made opposite map edges periodic neighbors. The owner approved
padded circulant embedding with centre cropping. V2.1 therefore regenerates
all arrays while retaining the approved coefficient, LOS-state, origin,
frequency, and seed policies.
