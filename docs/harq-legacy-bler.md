# Legacy BLER and HARQ Model

## Status and intended use

`harq_model: legacy_bler` is the default operational packet-error model. It restores FikoRE's historical table-driven BLER decision after hardening packet accounting, retry bounds, timers, queues, grants, random streams, and captured-traffic verdicts. `harq_model: disabled` is retained only for controlled ablation and diagnosis. The restored table is not presented as a calibrated 5G NR receiver model: its decoder, transport-block-size, code-block, redundancy-version, and source-link-simulation provenance are unavailable, so it supports repeatable comparative experiments rather than predictive BLER claims.

The original enabled implementation entered the repository in commit `58df02e4aebac5571e9762ffbe451e95e5aafe9a` on 11 May 2022. Commit `79a5344692df92b4b99d23751b9f059dea061413` on 26 November 2025 compiled the BLER decision out while addressing real-traffic hangs and explicitly described integer-bit handling as temporary. Commit `f2d74027978491aba8b14e9cf7f2ebe8a982cbb3` on 18 September 2026 still described BLER as compiled out. The current recovery treats that disable as a safety workaround, not a conclusion that radio failures should always ACK.

## Configuration

```ini
[MACLayer]
harq_model: legacy_bler
max_rtx_ul: 4
max_rtx_dl: 4
mcs_tables: true

[PHYLayer]
air_delay_var_ul: 0.0
rtx_period_ul: 0.004
rtx_period_var_ul: 0.0
rtx_proc_delay_ul: 0.002
rtx_proc_delay_var_ul: 0.0
air_delay_var_dl: 0.0
rtx_period_dl: 0.005
rtx_period_var_dl: 0.0
rtx_proc_delay_dl: 0.002
rtx_proc_delay_var_dl: 0.0
```

All delay values are seconds, finite, and non-negative. Active `legacy_bler` requires `mcs_tables: true`. `max_rtx` counts retransmissions after the initial attempt and may be zero. The final shipped profiles select `legacy_bler` explicitly; tests that isolate unrelated scheduler or transport behavior select `disabled` explicitly.

## Lookup table

The embedded table has shape `[2][5][4][28][60]`: two modulation tables, RBG sizes of 1, 2, 4, 8, or 16 PRBs, one through four layers, MCS indexes 0 through 27, and integer-ceiled SINR bins from −20 dB through the saturated upper bin. The packed little-endian float64 payload contains 67,200 values and has SHA-256 `d61acbe2a5cea399570c53b40f0374261ca28ac80ccefed0aee85728ea9bda70`. It contains 544 duplicate curves, no probability outside \([0,1]\), and no BLER increase between adjacent SINR bins. `tools/analyze_legacy_bler_table.py` reproduces these checks.

Every table axis is validated before lookup. Unsupported modulation indexes, RBG sizes, layer counts, MCS −1 or 28, and non-finite SINR are rejected rather than mapped to index zero or used for out-of-bounds access. Finite SINR beyond the tabulated range saturates at an endpoint, matching the historical binning rule.

## Historical failure law

For tabulated BLER \(p\) and zero-based transmission-attempt ordinal \(n\), where \(n=0\) is the initial attempt and \(n=1,\ldots,N_{\max}\) are retries, FikoRE preserves the historical failure law:

\[
P_{\mathrm{fail}}(p,n)=\operatorname{clip}_{[0,1]}\left[\left(1-(1-p)^{n+1}\right)0.8^{n-1}\right].
\]

This law is intentionally characterized rather than reinterpreted as Chase combining or incremental redundancy. In particular, \(P_{\mathrm{fail}}(0.1,0)=0.125\), \(P_{\mathrm{fail}}(0.1,1)=0.19\), and \(P_{\mathrm{fail}}(0.1,2)=0.2168\); for \(p=1\), the values begin \(1,1,0.8\). The non-monotonic early behavior is a known external-validity limitation. Finite probabilities are clamped, a zero probability always ACKs, and the random outcome stream is deterministic for a fixed run seed.

## Attempt and timer semantics

An initial failure with `max_rtx: 0` is immediately classified as `radio_dropped`. Otherwise the block is queued with retry ordinal one. Each failed retry either advances the ordinal exactly once or becomes `radio_dropped` when its ordinal equals the configured limit. The former hard-coded limit of four is removed.

Each queued block owns its own ready deadline. A retry delay is sampled from non-negative propagation, feedback-period, and processing components at the time of the current failed attempt; it is not anchored to the block's original creation time and is not stored in one global queue timer. The queue front defines readiness. BLER outcomes, propagation jitter, retransmission-period jitter, and processing-delay jitter use independent deterministic random streams, so changing timing variance cannot shift ACK/NACK outcomes.

## Bit and grant conservation

Packet sizes, fragments, HARQ blocks, queue occupancy, object fates, and cumulative counters use integer bits. Continuous scheduler capacity is quantized once at the PDCP boundary, with only its sub-bit residual carried forward. A call with \(0<g<1\) therefore either accumulates fractional capacity or makes integer progress; it cannot enter the former zero-progress loop.

A ready retry is transmitted only when the current integer grant can carry the complete stored block. Partial HARQ-block transmission is not modelled. If queued fresh data exists, a grant that is too small for the ready front retry may serve a new block on another logical HARQ process while the retry waits for sufficient capacity; this prevents one large ready block from pinning all useful service. The charged grant equals the bits actually transmitted, effective payload never exceeds charged bits, and unused integer capacity from that allocation unit is not moved to a later TTI. Rate-cap tokens are charged from actual transmitted bits rather than nominal candidate capacity. The original MCS and selected rank are retained for the block, while the current retry SINR is used with the original bounded table context.

The exact accounting invariant is

\[
B_{\mathrm{admitted}}=B_{\mathrm{delivered}}+B_{\mathrm{expired}}+B_{\mathrm{queue\ dropped}}+B_{\mathrm{radio\ dropped}}+B_{\mathrm{pending}}.
\]

The residual is exported through control state, monitoring, and text evidence logs. Pending bits include ingress, IP, HARQ, and release queues. HARQ and release queues are bounded and expose current occupancy, capacity or high-water marks, oldest age, and retry ordinal.

## Captured traffic

Captured packets use one per-UID completion state and one arrival-order queue for both success and failure. Fragments can complete in any order, but exactly one final `ACCEPT`, `ACCEPT_CE`, or `DROP` is sent for the original kernel packet. A dropped predecessor therefore cannot be appended behind a later success and create head-of-line deadlock. A failed netfilter verdict leaves userspace state intact and is retried; detach and shutdown convert every unresolved original to a terminal drop and use a bounded watchdog rather than hanging silently.

The netfilter receive thread treats `EAGAIN` as normal, drains already-readable netlink messages after stop is requested, joins before socket or buffer destruction, uses matching `malloc`/`free`, serializes verdict sends, and maintains synchronized 64-bit counters. Capture, pending-overflow-verdict, and completion queues are bounded; a persistent failure that exhausts the bounded shutdown policy is reported and terminates rather than silently closing unresolved state. Fake-capture tests cover mixed success/drop ordering, transient verdict failure, concurrent receive-stop-drain, detach, shutdown, ECN rewrite, and exact closure; privileged namespace execution remains environment-dependent because it requires Linux NFQUEUE and administrative network privileges.

## Validation and limitations

Deterministic tests cover BLER 0, 0.1, and 1; every attempt ordinal; `max_rtx` 0, 1, and 4; invalid table axes; grants smaller than, equal to, and larger than a stored block; independent deadlines; same-seed replay; random-stream independence; exact fragmentation and fate conservation; and sub-bit progress. A one-million-decision soak validates deterministic replay, and a 100,000-block queue cycle validates bounded progress. The HARQ and packet tests pass with `_GLIBCXX_ASSERTIONS`, AddressSanitizer, and UndefinedBehaviorSanitizer; the concurrent capture/shutdown test passes under ThreadSanitizer. The canonical profile campaign compares `disabled`, active `legacy_bler` with no retries, and active production retry limits under seed 20260927, with additional production seed sweeps.

The model still lacks a standards-traceable link-level calibration, TBS and code-block dependence, redundancy-version state, soft combining, frequency-selective effective SINR, codeword-specific MIMO state, and outer-loop link adaptation. Replacing the legacy law requires external link-level evidence and is a separate physical-model decision; operational hardening does not resolve that calibration gap.
