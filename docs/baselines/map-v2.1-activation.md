# Production V2.1 Map Activation Record

## Reason for regeneration

Adversarial validation found that the v2.0 same-size FFT filter treated
opposite production-map edges as periodic neighbors. The owner approved a
semantic correction: generate on a circulant grid twice as wide and crop one
central 291×291 field. This preserves local target covariance without wrapping
the opposite production edges together.

## Production identity

- Master seed: `20260927`
- Generator: `fikore-map-generator` version `2.1.0`
- Map semantic version: `2.1.0`
- Catalog entries: 21, including exact UMi 2.38 GHz
- Production catalog SHA-256:
  `57449bd7ea961aba3359be973ca34363102ca5ee3903b56a5b7336f6becdbcd0`
- Production manifest SHA-256:
  `0e3fe2f1b46584f786d2efa046bdd0bbbe8b17c1064d0fd239d971b96af269f1`

Two independent clean generations were byte-identical. Structural validation
rejects even v2 dimensions and any origin other than
`explicit-center-cell`.

## Statistical diagnostics

Thirty independent master seeds were evaluated for every catalog entry
(630 realizations). The largest observed diagnostics were:

- absolute bias of the ensemble-mean radial LOS probability: 0.038;
- error of the ensemble-mean axial shadow autocorrelation at the nearest
  representable lag: 0.007;
- absolute ensemble-mean opposite-edge correlation: 0.026.

These are descriptive maxima, not predeclared population bounds. Pointwise
confidence intervals are not simultaneous, and shadow variance is normalized
by construction.

The shopping-mall LOS probability remains a labelled legacy heuristic with
pending source provenance. ABG source-fit validity ranges also remain
unencoded; map extent must not be interpreted as a validated model range.
