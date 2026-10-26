# NetworkBackend and SFV Integration — As Built

**Status:** implemented and exercised with SFV-VQEG v0.7.2
**Code:** `fikore_transport/backend.py`,
`transport/benchmarks/validate_sfv.py`
**Tests:** `test_backend.py`, `test_fikore_link.py`

## Boundary

The implemented Python backend exposes:

```python
submit_request(ue_id, request_id, bytes_total)
cancel_request(ue_id, request_id)
advance() -> NetworkStep
set_ue_control(ue_id, UeControl(...))
```

`BackendConfig` sets direction, ECN, congestion controller, receive window,
TCP connection mode, window duration and optional final horizon. The SFV bridge
translates between the external repository's generic operations and this
backend; it does not forward `fikore-control-1` messages to Node.js.

```text
SFV player
   │ generic downloads, cancellations and NetworkStep data
   ▼
Python bridge
   │ TransportBackend methods
   ▼
TransportBackend + Runner
   │ Transmit / Arrival
   ▼
FikoreLink
   │ fikore-control-1
   ▼
FikoRE emulator
```

## Request mapping

`TransportBackend.submit_request()`:

1. rejects a reused `(ue_id, request_id)`;
2. reuses an idle TCP connection for the UE when persistent mode is selected;
3. otherwise registers a new flow-to-UE mapping and sender/receiver pair;
4. assigns the object's byte range in that connection's sequence space;
5. queues the byte count and adds new flows to the shared Runner.

Persistent mode is the default and behaves as an HTTP/1.1-style pool. Concurrent
objects use separate connections when no idle connection is available. Fresh
mode preserves one initialized TCP flow per object; ideal transport remains
per-object.

During `advance()`, connection-wide receiver progress is translated through the
object's byte range into `DownloadProgress`. Delivery of the full range becomes
`DownloadCompleted`. Cancellation becomes `DownloadCancelled` only after the
already-submitted data tail drains, and the cancelled connection is not reused.

Application request IDs are opaque. Link segment tags are generated
independently and are not visible to the SFV process.

## Time and final step

The first `advance()` returns an empty `NetworkStep` at 0 s. Later calls advance
by `window_ttis` (10 ms in the validation), except that `horizon_ttis` clamps
the final step. `NetworkStep.is_final` is set at that horizon; another advance
after the final step is an error.

Every UE shares this Runner clock. Requests submitted before a window therefore
compete concurrently rather than running as serial per-UE experiments.

## UE mapping and controls

The validation sorts external UE IDs and maps them to physical FikoRE indices.
`FikoreLink` also supports explicit sparse mappings and textual configured
targets.

`set_ue_control()` currently maps `UeControl.priority` and
`UeControl.rmax_mbps` to FikoRE runtime parameters. Mobility fields are
available through lower-level `FikoreLink.set_params()` but are not fields of
this `UeControl` dataclass.

## SFV v0.7.2 validation

`transport/benchmarks/validate_sfv.py` loads the external bridge module and
substitutes `TransportBackend` for its deterministic mock:

- the Node.js engine emits generic downloads and cancellations;
- the Python bridge calls backend submit/cancel/advance methods;
- two logical UEs share one FikoRE cell and clock;
- B1 and B2 preserve their existing JavaScript behaviour;
- swipes exercise cancellation and transport-tail accounting;
- output includes run result, NetworkStep transcript, manifest and per-UE
  session records;
- the manifest records the effective transport and TCP connection mode.

Run from the repository root:

```bash
PYTHONPATH=transport python3 transport/benchmarks/validate_sfv.py --help
```

The external SFV and SFV-core checkouts are supplied with
`--sfv-vqeg-root` and `--sfv-core-root`. The script records both revisions and
the tested FikoRE revision.

## Evidence and interpretation

- Backend request/cancel/time behaviour:
  `transport/tests/test_backend.py`
- Real emulator mapping/accounting:
  `transport/tests/test_fikore_link.py`
- External integration:
  `transport/benchmarks/validate_sfv.py`
- Reviewed aggregate of parity, five-second and cancellation runs:
  `transport/benchmarks/results/sfv-pilot.json`

The aggregate is marked `artifact_kind: curated_summary`; native script runs
write generated manifests beneath the requested output directory.

This evidence establishes compatibility with the checked v0.7.2 example. It
does not establish browser equivalence, Linux TCP equivalence or coverage of
every external harness scenario.
