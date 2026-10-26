# External TCP Controller Validation

**Status:** manually reproducible external comparison
**Runner:** `transport/benchmarks/validate_tcp_references.py`
**Result:** `transport/benchmarks/results/tcp-reference.json`

Internal tests establish repository consistency. This validation compares the
controller equations and transitions with independent public references.

## References

- Reno: RFC 5681 and `TCPReno` from TL-System ns.py v0.4.5.
- CUBIC: RFC 8312/9438, Linux conventions (`C=0.4`,
  `beta_cubic=0.7`) and `TCPCubic` from ns.py v0.4.5.
- RTO: RFC 6298 estimators, with the model's documented Linux-style 200 ms
  minimum instead of the RFC's conservative 1 s floor.
- Prague: external `L4STeam/udp_prague/prague_cc.cpp`, compiled unchanged
  behind the local C ABI shim.

The runner drives the local and ns.py controllers through identical
ACK/loss sequences and evaluates independent RFC vectors.

## Reproduction

Clone the pinned ns.py release outside the FikoRE tree:

```bash
git clone --depth 1 --branch v0.4.5 \
  https://github.com/TL-System/ns.py.git /tmp/ns.py
```

From the FikoRE checkout:

```bash
make -C transport/prague
PYTHONPATH=transport python3 \
  transport/benchmarks/validate_tcp_references.py \
  --ns-root /tmp/ns.py \
  --output transport/benchmarks/results/tcp-reference.json
```

The result records the exact ns.py commit and the FikoRE revision tested.
Use another output path for exploratory runs; replace the checked result only
after reviewing all vectors and provenance.

## Acceptance checks

- Reno congestion window and slow-start threshold match for 500 ACKs, fast
  retransmit/recovery and RTO transitions.
- CUBIC matches for 1000 ACKs within one MSS; the checked run records zero-byte
  maximum congestion-window error.
- The CUBIC curve point at `W=100 MSS`, `t=1.05 s` matches
  `100 + 0.4 * 1.05^3`.
- Multiplicative decrease retains `beta_cubic=0.7`.
- RFC 6298 samples `(100 ms, 120 ms)` produce expected SRTT, RTTVAR and RTO.
- Prague loads the external reference implementation and reports configured
  MSS and initial window.

## Interpretation

The comparison validates congestion-controller state transitions and equations
under deterministic input sequences. It does not claim packet-for-packet
equivalence with a complete Linux TCP stack.

Examples of full-stack behaviour outside this model include SYN/FIN processing,
HyStart++, PRR, RACK/TLP, Linux pacing details, socket buffers, qdisc behaviour
and NIC offloads. Exact limitations are maintained in
[`LIMITATIONS.md`](LIMITATIONS.md).

The external repositories are not vendored, so this runner is manual rather
than part of the default test suite. Optional CI integration is tracked in
[`FUTURE-ROADMAP.md`](FUTURE-ROADMAP.md).
