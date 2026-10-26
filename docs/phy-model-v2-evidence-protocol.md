# PHY Model V2 Evidence Protocol

## Purpose

This protocol defines the claims, controls, statistics, and artifact
requirements for the FikoRE PHY Model V2 paper. It was formalized after the
initial candidate-map comparison and activation decision, so it is a
retrospective reproducibility protocol rather than a preregistration. It was
applied before the final adversarial-review reruns.

The protocol baseline is commit
`dc81da62f59ca93e36a02c36bdcecd45ec6cd2bd` on
`feature/phy-model-v2`. The model baseline inherited from `dev` is commit
`e995563fd3e8fa300f4b0accca3b494f7177101c`.

## Falsifiable claims

| ID | Claim | Required evidence | Failure condition |
|---|---|---|---|
| C1 | Signal, noise, and interference use one per-PRB reference independent of scheduling grouping for a deterministic channel. | Equation audit, power-model unit tests, and a runtime grouped/per-PRB SINR experiment with the same physical carrier PRBs and fading disabled. | Equivalent physical carrier power, noise, and deterministic channel produce a grouping-dependent per-PRB SINR. Incomplete final RBG capacity and scheduling-unit fading resolution are reported separately rather than claimed invariant. |
| C2 | Two-stage UL scheduling respects the UE total-power cap while exposing a deterministic nominal rate to the scheduler. | Unit tests for nominal and finalized powers plus a power-conservation sweep. | Final grants exceed the configured UE power or the same deterministic inputs produce different allocations. |
| C3 | PF and BET service history is updated once per active TTI and is independent of CQI report cadence, while MT and RR never enter the history lifecycle. | State and scheduler tests plus CQI-period invariance and alias-capability tests. | EWMA state differs solely because static-channel CQI cadence changes, or MT/RR acquires history or provisional penalties. |
| C4 | Allocation-unit reranking improves short-horizon MAC effective-service continuity in grouped full-backlog PF/BET grids at measured cost. | Scheduler-family benchmark with `none` and `allocation_unit`, Jain fairness, maximum effective-service gap, and repeated per-TTI runtime samples. | No short-horizon continuity benefit is observed or measured cost exceeds the stated operating envelope. |
| C5 | V2 maps are byte-deterministic and origin-consistent. | Independent regeneration, schema/runtime origin tests, and 30-realization diagnostics. | Equal inputs produce different bytes or runtime origin differs from generator origin. LOS, covariance, seam, and link-gain statistics are descriptive diagnostics, not a formal model-validity acceptance test. |
| C6 | Activating V2 maps changes only propagation inputs and produces explainable deltas in the five canonical profiles. | Common-seed, one-factor-at-a-time legacy/V2 comparisons. | Non-map configuration differs, candidate and promoted bytes differ, or output deltas cannot be traced to changed link gain. |
| C7 | Active legacy HARQ is bounded, deterministic, and conserves integer bits and charged grants exactly. | Probability and table-axis tests, retry-limit sweeps, packet properties, one-million-decision replay, queue soak, and zero residual telemetry. | Unsupported table access, unbounded queue growth, zero-progress processing, effective bits above charged bits, or nonzero closure residual. |
| C8 | Captured traffic receives exactly one terminal verdict per UID and cannot deadlock behind a dropped predecessor. | Fake-capture ordering, verdict-failure retry, detach/shutdown, ECN, and queue-progress tests; privileged NFQUEUE runs where available. | Duplicate or missing final verdict, userspace state erased after send failure, unresolved shutdown state, or watchdog stall. |

Passing these checks establishes internal consistency and reproducibility. It
does not establish deployment-wide predictive validity.

## Experimental controls

- Use deterministic RNG seeds and record every derived map seed.
- Use common random numbers for paired legacy/V2 and scheduler comparisons.
- Change one modeled factor at a time whenever the implementation permits it.
- Keep traffic, mobility, scheduler, duration, and packet settings identical
  in paired five-profile runs.
- Separate independent map realizations from correlated spatial samples.
- Report arithmetic definitions for every aggregate and denominator.
- Preserve raw logs outside Git; commit compact CSV/JSON summaries and the
  exact input configurations.
- Record source commit, dirty state, compiler, Python and NumPy versions,
  hostname, CPU model, kernel, command line, wall time, and return code.

## Canonical profile protocol

The canonical profiles are:

1. `offline_umi_n40_npn.ini`;
2. `offline_uma_n78_pedestrian.ini`;
3. `offline_rural_n78_vehicular.ini`;
4. `offline_indoor_hotspot_n78_pedestrian.ini`;
5. `offline_umi_n258_fwa.ini`.

All five general-purpose study UEs use `l4s_dual_queue: false`; L4S is enabled only in dedicated comparison profiles. Historical rendered inputs remain immutable, including older rural runs that intentionally recorded the previous L4S-enabled study queue.

The HARQ stress extension adds `offline_umi_n258_fwa_high_loss.ini`. Each production-map run uses seed `20260927` and 180 simulated seconds. Summary
statistics exclude the first 20 seconds when the timestamped source metric
supports warm-up filtering; metrics that cannot be filtered are explicitly
labelled as whole-run values. The report includes, by direction:

- offered, delivered, and error throughput;
- SINR and MCS quantiles;
- PHY-outage UE count;
- demand satisfaction;
- grant-to-payload efficiency;
- zero-delivery fractions in 10 ms, 100 ms, and 1 s windows;
- delivery-gap P50/P95/P99 and maximum inside contiguous positive-offer
  segments for non-outage UEs;
- wall-clock runtime.
- retransmitted and radio-dropped bits;
- HARQ queue occupancy, high-water mark, oldest age, and retry ordinal;
- exact admitted-versus-terminal-plus-pending conservation residual.

An outage UE is one whose logged MCS is below zero in at least 99% of radio
samples. Queue and HARQ occupancy are logged, but delivery gaps combine traffic, eligibility, scheduling, retries, expiry, and release timing and therefore must not be labelled scheduler starvation without additional conditioning. Starvation is never inferred from a single TTI.

## Map ensemble protocol

The production master seed is `20260927`. Statistical validation uses 30
independent master seeds per scenario-frequency catalog entry; one map is
generated and discarded at a time.

For each realization, record:

- empirical LOS probability in fixed radial bins;
- LOS and NLOS shadow mean and standard deviation;
- axial shadow autocorrelation at fixed physical lags, including the declared
  decorrelation distance;
- opposite-edge correlation to detect periodic FFT seams;
- final link-gain P05, P50, and P95;
- finite-value, dimension, origin, and metadata checks;
- a profile-specific coverage proxy where a canonical link budget exists.

Report the ensemble mean and a pointwise two-sided 95% confidence interval over
independent realizations. Spatial cells are not counted as independent
replicates. Intervals are descriptive and not adjusted for simultaneous
coverage. They characterize generator variability, not field-measurement
uncertainty or a predeclared equivalence test.

## Runtime protocol

Functional results and timing results are separate. Timing uses the optimized
build, disabled verbose logging, a single emulator thread, repeated samples,
and the named host recorded in the evidence manifest. Time every warmed-up TTI
individually and report empirical P50, P95, P99, maximum, and the fraction over
1,000 microseconds. Any real-time statement is limited to the tested host and
case; ten process repetitions do not establish a general latency tail.

## Paper structure

The final paper uses the following structure:

1. contributions and falsifiable claims;
2. formal system model and approximation boundary;
3. identified defects and accepted corrections;
4. experimental methodology and artifact manifest;
5. controlled results;
6. open Phase 3 option matrix;
7. limitations and conclusion.

Accepted decisions are presented with rationale and measured consequences.
Unresolved Phase 3 topics are presented as alternatives with state, accuracy,
runtime, calibration requirements, and external references; they are not
presented as implemented features.

## Review gates

The manuscript must pass independent propagation, PHY/link-abstraction,
MAC/statistics/runtime, and hostile editorial reviews. A finding is blocking if
it identifies a dimensionally invalid equation, unsupported causal claim,
irreproducible result, incorrect citation, hidden confounder, or materially
misleading validation claim. At least two complete review rounds are required,
with a maximum of four.
