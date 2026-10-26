# PHY Model V2 Decision Gate

**Status:** Approved on 2026-09-24
**Required before:** Any profile or runtime behavior change

## 1. Confirmed context

- ABG remains the path-loss family.
- RMa is represented as one 100 MHz n78 component carrier in the initial
  implementation.
- The 26 GHz profile is renamed n258 and represented as one 400 MHz,
  120 kHz-SCS component carrier.
- Carrier aggregation is deferred.
- PF service history is updated once per 1 ms emulator TTI, independently of
  CQI refresh.
- Intra-TTI reranking remains experimental until benchmarked.
- Legacy PF runtime compatibility is not required.

## 2. Decision A — power and gain semantics

### 2.1 Proposed field semantics

- `tx_power`: aggregate conducted transmit power for one configured component
  carrier, before antenna gain.
- `eNB_gain`: equivalent serving-link gNB antenna/array gain.
- `UT_gain`: equivalent serving-link UE/CPE antenna/array gain.
- `power_boost`: reference-signal-only offset used for RSRP, not PDSCH/PUSCH
  traffic SINR.
- `enb_noise_figure` and `ut_noise_figure`: receiver noise figures at the
  corresponding endpoint.

The resulting scalar link budget can approximate array gain, but does not
model beam state, pattern, pointing loss, blockage, or sidelobes.

### 2.2 Proposed initial table

| Profile | Carrier | gNB power | gNB gain | UE gain | gNB NF | UE NF | Status |
|---|---|---:|---:|---:|---:|---:|---|
| UMi n40 | 20 MHz, μ1 | 43 dBm | 8.7 dBi | 0 dBi | 2 dB | 9 dB | Keep current baseline |
| UMa n78 | 100 MHz, μ1 | 46 dBm | 8.7 dBi | 0 dBi | 2 dB | 9 dB | Keep current baseline |
| RMa n78 | 100 MHz, μ1 | **46 dBm** | 8.7 dBi | 0 dBi | 2 dB | 9 dB | Proposed macro baseline |
| Indoor n78 | 100 MHz, μ1 | **24 dBm** | 8.7 dBi | 0 dBi | 2 dB | 9 dB | Proposed local-area baseline |
| n258 FWA | 400 MHz, μ3 | **35 dBm** | **24 dBi** | **26 dBi** | **7 dB** | **10 dB** | Proposed equivalent-array baseline |

The n258 values are an expert-review baseline, not a product-specific
calibration. The approximate DL EIRP is 59 dBm before any additional losses.

**Decision:** Approved as proposed.

### 2.3 Proposed FWA profile set before O2I redesign

The current runtime cannot cleanly select low-loss versus high-loss facade
models. Therefore Block 0 should add only:

1. `outdoor_cpe`: no O2I wall loss;
2. `high_loss_indoor_stress`: current high-loss O2I behavior.

The low-loss/window profile should wait for the supervised O2I work in Block
2 rather than encoding wall loss indirectly through antenna gain.

**Decision:** Approved. Block 0 will provide outdoor and high-loss stress
profiles only.

## 3. Decision B — resource-level signal and noise representation

### 3.1 Downlink recommendation

Use a per-PRB power spectral representation:

\[
P_{\mathrm{DL,PRB}}
=P_{\mathrm{DL,total}}
-10\log_{10}(N_{\mathrm{PRB,total}})
\]

\[
N_{\mathrm{PRB}}
=N_0+NF+10\log_{10}(12\Delta f)
\]

Calculate per-PRB SINR, then use the scheduling-unit RBG size for the matching
SINR-BLER/MCS table and multiply capacity by the RBG subcarrier count.

Equivalent grouping of the same PRBs must not change deterministic per-PRB
power/noise SINR. The current small-scale fading resolution follows scheduling
units and is a separately documented approximation.

### 3.2 Interference recommendation

Represent interfering power with the same per-PRB reference bandwidth. Apply
`interfered_bandwidth_ratio` as resource-overlap probability or PSD scaling,
not as an undocumented total-power correction. The selected interpretation
must be exposed in documentation and tests.

### 3.3 Uplink decision required

`tx_power_ul=23 dBm` is physically a total UE power limit, while the scheduler
evaluates individual allocation units before the UE's complete TTI allocation
is known.

Options:

1. **Per-allocation total power:** distribute the UE power over each candidate
   allocation unit. Simple, but assigning multiple units can reuse the total
   power and overestimate aggregate UL power.
2. **Per-PRB PSD abstraction:** interpret the configured value through a
   reference bandwidth/PSD. Efficient and grouping-invariant, but 23 dBm no
   longer directly means the UE total-power cap.
3. **Two-pass TTI allocation:** allocate resources, then distribute the UE
   total power over its complete allocation and recompute UL SINR/MCS/grant.
   Most physically coherent, but materially increases scheduler complexity.

The selected implementation is recorded below.

**Decision:** Use allocation-aware two-stage UL finalization without
rescheduling:

1. The scheduler uses nominal per-PRB power, corrected by the previous TTI's
   allocation when the UE is power-limited.
2. After all UL assignments are known, calculate the current total allocated
   PRBs per UE.
3. Recompute total/per-PRB power, SINR, MCS, and grant size.
4. Do not rerun scheduling in the same TTI.

For fractional power control:

\[
P_{\mathrm{nominal,PRB}}=P_0+\alpha PL
\]

\[
P_{\mathrm{total}}=
\min(P_{\max},P_{\mathrm{nominal,PRB}}+10\log_{10}M)
\]

\[
P_{\mathrm{actual,PRB}}=
P_{\mathrm{total}}-10\log_{10}M
\]

For fixed-power mode, the configured value remains a total UE power and is
distributed over the allocated PRBs. Simplified inter-cell UL interference
remains a documented per-PRB PSD approximation until the multicell work.

### 3.4 Thermal-noise configuration

Recommendation: honor `thermal_noise` as noise-density input in dBm/Hz, with
default `-174`, rather than parsing and ignoring it.

**Decision:** Approved.

## 4. Decision C — PF state and configuration

### 4.1 Proposed configuration

Under `[MACLayer]`:

```ini
pf_alpha: 1.0
throughput_time_window_ms: 100.0
throughput_intra_tti_update: none
```

`throughput_intra_tti_update` remains experimental until the benchmark decision.

### 4.2 Proposed EWMA behavior

\[
\bar R_i(t+1)=(1-a)\bar R_i(t)+a\,x_i(t),
\qquad
a=1-e^{-1\,\mathrm{ms}/\tau}
\]

- Update once after every MAC TTI.
- `x_i` is effective payload served in that TTI.
- Enabled, backlogged, valid-MCS UEs that receive no service contribute zero.
- Idle UEs with no backlog freeze their EWMA.
- Detached UEs reset scheduler state.
- Reattached UEs use cold-start initialization.

### 4.3 Proposed cold start

Initialize \(\bar R_i\) to the UE's current standalone achievable rate,
clamped by epsilon. This avoids an unbounded new-UE metric while starting
equal-rate UEs at equal PF ratios.

Alternative: initialize to epsilon, intentionally giving newly active UEs a
short-term boost.

### 4.4 Legacy field migration

- Introduce cell-level `pf_alpha`.
- Keep or rename the existing beta only for BET if required.
- Reject per-UE `beta_metric` when `metric_type` selects PF after the migration
  period; do not silently ignore it.
- Update all canonical configs and documentation in the same commit.

**Decision:** Approved as proposed, including `pf_alpha=1`,
`throughput_time_window_ms=100`, standalone-rate cold start, idle-state freeze,
zero-throughput updates for active unscheduled UEs, and reset on detach.

## 5. Decision D — approval scope

Approval of this packet authorizes:

- Block 0 profile changes using the selected table;
- Block 1A implementation using the selected signal/noise model;
- Block 1B common-alpha, TTI-updated PF implementation.

It does not authorize:

- semantic map/O2I changes;
- multicell, beamforming, CA, or MIMO redesign.

The intra-TTI benchmark decision was approved separately on 2026-09-25:
canonical grouped-RBG PF profiles use `allocation_unit`, while the parser
default remains `none` for custom and per-RB configurations.
