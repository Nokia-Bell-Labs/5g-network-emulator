# Future Transport Roadmap

Everything in this file is **not implemented** unless an entry explicitly says
otherwise. The list records candidate increments; it is not a delivery
commitment.

## Promotion rule

An item leaves this file only when:

1. its acceptance criteria pass;
2. named automated tests and reproducible evidence exist;
3. current limitations are updated or removed;
4. the relevant numbered as-built document describes the shipped interface;
5. result provenance identifies the tested revision.

Design prose alone is not sufficient.

## UDP in TransportBackend

**Motivation:** let a generic NetworkBackend experiment select open-loop UDP
without dropping to Runner internals.

**Scope:** request lifecycle, direction, progress, cancellation and accounting
for UDP objects; no implied reliability or congestion control.

**Acceptance criteria:**

- `submit_request`, `cancel_request` and `advance` preserve current API;
- delivered/lost/expired byte accounting conserves offered bytes;
- cancellation tail is explicit;
- multi-UE UDP and mixed TCP/UDP advance on one clock.

**Required evidence:** backend unit tests and a FikoRE multi-UE scenario JSON.

**Documentation on promotion:** `01-scope-and-boundary.md`,
`06-application-patterns.md`, `07-network-backend-and-sfv.md`, and
`LIMITATIONS.md`.

## Multiplexed objects on one TCP connection

**Motivation:** extend the implemented HTTP/1.1-style persistent pool with
HTTP/2-like concurrent object scheduling on one connection.

**Scope:** explicit connection identity, frame/byte attribution for interleaved
objects, scheduling policy, per-object cancellation without retiring the
connection, and conserved shared-connection accounting.

**Acceptance criteria:**

- multiple active object IDs share one sender and congestion window;
- scheduling and completion are not forced into submission order;
- cancellation of one object does not corrupt or stop another;
- stream-wide ACK/SACK recovery preserves per-object accounting;
- persistent-pool and fresh modes remain available.

**Required evidence:** deterministic multiplexing and cancellation tests plus an
SFV comparison against the persistent pool.

**Documentation on promotion:** `04-transport-model.md`,
`06-application-patterns.md`, `07-network-backend-and-sfv.md`, `LIMITATIONS.md`
and the protocol-specific scope statement.

## Generic interval recorder and export

**Motivation:** avoid scenario-specific metric assembly and make flow/network
time series reproducible.

**Scope:** common per-flow, per-object, per-UE and Link interval snapshots with
JSON/CSV export and metadata.

**Acceptance criteria:**

- configurable interval and field schema;
- cumulative totals equal terminal model counters;
- no state mutation caused by observation;
- bounded memory or streaming output for 300 s runs;
- schema version and source revision recorded.

**Required evidence:** snapshot unit tests and regenerated scale result with a
schema validator.

**Documentation on promotion:** `06-application-patterns.md`,
`08-validation-evidence.md`, result-schema documentation and
`transport/README.md`.

## Automated simulated-versus-real iperf comparison

**Motivation:** quantify where the research model agrees with and differs from
a real host TCP/UDP stack.

**Scope:** controlled namespace/container topology, pinned kernel/tool metadata,
matching delay/rate/loss configuration and comparison of throughput/RTT/loss
time series.

**Acceptance criteria:**

- setup is scripted and reports skipped prerequisites clearly;
- real and simulated parameters are recorded side by side;
- tolerance bands are justified per metric;
- failures distinguish environment setup from model deviation;
- results never claim universal Linux equivalence.

**Required evidence:** one loss-free and one congested reproducible campaign,
with raw machine-readable outputs.

**Documentation on promotion:** `08-validation-evidence.md`,
`09-external-tcp-validation.md`, `LIMITATIONS.md`, and the reproducible-runners
section of `transport/README.md`.

## Simultaneous Prague and CUBIC coexistence

**Motivation:** validate coupled DualPI2/L4S behaviour when scalable and classic
flows share the same bottleneck.

**Scope:** at least one Prague ECT(1) flow and one CUBIC classic flow active
simultaneously under a versioned FikoRE DualPI2 configuration.

**Acceptance criteria:**

- both flows overlap for a defined steady interval;
- CE, drop, throughput and queue data are attributable per flow/class;
- byte conservation passes;
- repeated deterministic runs agree;
- expected coexistence criteria are stated before evaluating results.

**Required evidence:** scenario configuration, runner output and checked JSON
with per-flow time series/summary.

**Documentation on promotion:** `05-congestion-control.md`,
`08-validation-evidence.md`, `LIMITATIONS.md`, and results index.

## Multi-TTI grant with `break_on_event`

**Motivation:** reduce protocol round trips while returning promptly when a
transport-relevant event occurs.

**Scope:** optional grant predicate and early-stop response carrying actual
boundary and triggering event/cursor.

**Acceptance criteria:**

- existing absolute grants retain identical semantics;
- early stop never advances beyond the reported TTI;
- no event is lost or duplicated across early stop, replay or reconnect;
- multi-command acknowledgement framing remains synchronized;
- cancellation/completion latency is bounded and tested.

**Required evidence:** C++ protocol tests, Python FikoreLink tests and a
round-trip benchmark against fixed-step grants.

**Documentation on promotion:** `02-clock-and-lockstep.md`,
`03-link-and-control-protocol.md`, runtime protocol docs and `LIMITATIONS.md`.

## Idle skipping with `Scheduler.next_tti()`

**Motivation:** avoid visiting TTIs in which no timer, action, feedback or Link
work can change state.

**Scope:** wire scheduler knowledge into model advancement while preserving
exact fixed-step outcomes.

**Acceptance criteria:**

- skipped and unskipped runs produce identical event ordering and counters;
- RTO, pacing, ACK and Link deadlines prevent unsafe skips;
- active FikoRE cells are not skipped without a compatible grant mechanism;
- a sparse workload demonstrates fewer executed steps.

**Required evidence:** differential property tests over seeded workloads and a
performance benchmark.

**Documentation on promotion:** `02-clock-and-lockstep.md`,
`08-validation-evidence.md` and `LIMITATIONS.md`.

## Runtime MSS verification

**Motivation:** prevent silent inconsistency between transport segmentation and
the emulator packet-size configuration.

**Scope:** read or negotiate the effective FikoRE packet size during Link setup
and reject incompatible transport MSS, with an explicit override only if its
semantics are defined.

**Acceptance criteria:**

- matching configuration starts normally;
- mismatch fails before data injection with a clear diagnostic;
- textual/numeric UE mappings do not affect the check;
- LoopbackLink remains independently configurable.

**Required evidence:** protocol/config unit test and FikoreLink integration
test for match and mismatch.

**Documentation on promotion:** `01-scope-and-boundary.md`,
`03-link-and-control-protocol.md`, configuration docs and `LIMITATIONS.md`.

## Optional external-repository CI

**Motivation:** detect drift against ns.py, Prague and SFV without making
network access a prerequisite for the default suite.

**Scope:** opt-in CI jobs with pinned revisions, caches and explicit provenance.

**Acceptance criteria:**

- default offline tests remain hermetic;
- external jobs pin commits rather than moving branches;
- unavailable upstream services produce a distinct infrastructure result;
- generated evidence is retained as an artifact;
- dependency revision updates require reviewed result changes.

**Required evidence:** successful clean CI runs for TCP reference and SFV
integration plus a documented local reproduction.

**Documentation on promotion:** `07-network-backend-and-sfv.md`,
`08-validation-evidence.md`, `09-external-tcp-validation.md`,
`transport/README.md`, and `LIMITATIONS.md`.
