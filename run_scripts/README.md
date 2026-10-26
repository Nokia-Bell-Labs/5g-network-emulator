# Run Scripts

This directory contains the scripts used to create and reuse the FikoRE `NFQUEUE` lab.

The lab and the emulator config are separate concerns:

- the lab manages Linux namespaces, `veth` pairs, routes, and `NFQUEUE` rules;
- the `.ini` file remains the source of truth for what FikoRE actually instantiates;
- `run` uses `CONFIG_FILE` as-is and does not generate or patch a runtime copy.

## Scripts

- [run_fikore_nfqueue_lab.sh](run_fikore_nfqueue_lab.sh): creates, reuses, inspects, and tears down the lab topology, then launches FikoRE on top of it.
- [run_docker.sh](run_docker.sh): builds or launches the helper container for Docker backends.
- [run_offline.sh](run_offline.sh): runs an offline simulation from a positional config file and then generates plots from the latest logs. By default it keeps only the general offline plots; pass `-a` to also generate DualQ/L4S/NFQUEUE-specific plots.
- [run_iperf.sh](run_iperf.sh): runs an offline iperf-shaped session through `fikore-iperf3`. It creates `transport/.venv` on first use and forwards every argument.
- [run_emulated.sh](run_emulated.sh): runs the NFQUEUE lab workflow with a positional emulation config file.
- [run_api.sh](run_api.sh): starts the control API, which serves the web dashboard on `http://localhost:8100/` and collects telemetry over UDP.
- [python_env.sh](python_env.sh): shared Python environment helper used by the other run scripts.
- [fikorens](fikorens): direct wrapper that opens a namespace shell without requiring `source`.
- [fikorens-open](fikorens-open): direct wrapper that opens a namespace shell and returns when it exits.
- [fikorens-exec](fikorens-exec): direct wrapper for one-shot commands inside a namespace.
- [fikore_ns.sh](fikore_ns.sh): optional sourced helper exposing `fikorens`, `fikorens_open`, and `fikorens_exec`.

## Mental Model

- `up`: explicitly recreate the lab topology and save its state.
- `run`: launch FikoRE on the current lab. If no lab exists yet, it creates one first. If saved state exists but the topology is broken, it fails and tells you to run `up`.
- `status`, `shell`, `exec`, `capture-*`, `ue-add`, `ue-del`, and `down`: reuse the saved lab state. They do not need `BACKEND` again once the lab is up.

This avoids the old destructive behavior where `run` recreated namespaces every time.

For real-traffic emulation, use [run_emulated.sh](run_emulated.sh) or the lab script directly.
For offline simulated traffic, use [run_offline.sh](run_offline.sh).
For an offline iperf-shaped session on a scenario, use [run_iperf.sh](run_iperf.sh).

## Topology

### Emulated Plane

UE `<->` DN traffic always crosses the `EMU` node, which is where FikoRE runs and where the `NFQUEUE` rules are applied.

```text
fkue1 ----\
fkue2 ----- EMU ---- fkdn
...       /
```

Default addressing:

- UE1 `<->` EMU: `10.201.1.2/30 <-> 10.201.1.1/30`
- UE2 `<->` EMU: `10.201.2.2/30 <-> 10.201.2.1/30`
- DN  `<->` EMU: `10.255.0.2/24 <-> 10.255.0.1/24`

Default queue mapping:

- UE1: UL `100`, DL `101`
- UE2: UL `110`, DL `111`

General formula:

- `UL = QUEUE_BASE + (UE - 1) * QUEUE_STEP`
- `DL = UL + 1`

With the defaults (`UE_COUNT=2`, `QUEUE_BASE=100`, `QUEUE_STEP=10`), `UE3` would use `120/121` only if `UE_COUNT>=3` or if you add it later with `ue-add 3`.

### Management Plane

This plane only exists in `docker-none`.

```text
host <----> FikoRE container
172.30.0.1      172.30.0.2
```

It is only a management link. If your `.ini` emits telemetry UDP traffic from inside the container, that `.ini` must already point to a reachable host address such as `172.30.0.1:8098`.

## Requirements

- Run [run_fikore_nfqueue_lab.sh](run_fikore_nfqueue_lab.sh) through `sudo`.
- Have `bin/fikore` already built.
- For Docker backends, have an image built and a container started with [run_docker.sh](run_docker.sh).
- For Docker backends, keep `CONFIG_FILE` inside the repository so the container can read that same file through the bind mount.

## Recommended Example Config

The best ready-to-use emulated example is [`config/emulated_rural_n78_single_with_background.ini`](../config/emulated_rural_n78_single_with_background.ini).

Why this file is the best default example:

- it matches the lab queue plan directly with `UE1 -> 100/101`;
- it keeps `l4s_dual_queue: false`, so unattended baseline runs do not silently enable DualQ behavior;
- it adds simulated background users to create load around the captured UE;
- it already enables UDP telemetry output on `127.0.0.1:8098`, which is where the API collector listens.

Use `config/emulated_uma_n78_compare_with_background.ini` or `config/emulated_umi_n40_npn_compare_with_background.ini` when the experiment explicitly compares an L4S captured UE with a legacy captured UE.

Important:

- `run_fikore_nfqueue_lab.sh` does not rewrite monitoring keys anymore.
- For `host` and `docker-host`, a collector on `127.0.0.1:8098` is usually fine if the `.ini` already says that.
- For `docker-none`, the `.ini` should explicitly target the host-side management IP, typically `172.30.0.1:8098`.
- Adding namespaces with `ue-add` does not modify the `.ini`; FikoRE only uses UEs actually defined in the config file.

## Quick Workflow

All commands below assume your current directory is the repository root.

### 1. Create the Lab

For the recommended example config:

```bash
sudo BACKEND=host UE_COUNT=2 run_scripts/run_fikore_nfqueue_lab.sh up
sudo run_scripts/run_fikore_nfqueue_lab.sh status
```

This creates two namespace UEs. The selected `.ini` determines whether either captured path uses L4S; the recommended rural baseline leaves it disabled.

If you need a different lab size, change `UE_COUNT` only when running `up`. Later `run` reuses the existing topology.

### 2. Open Namespace Shells

Direct wrappers work without `source`:

```bash
run_scripts/fikorens-open dn
run_scripts/fikorens-open ue1
run_scripts/fikorens-open ue2
```

For one-shot commands:

```bash
run_scripts/fikorens-exec dn iperf3 -s -p 5202
run_scripts/fikorens-exec ue1 iperf3 -c 10.255.0.2 -p 5202 -t 30
```

Each interactive namespace shell adds a prompt prefix such as `(ue1)` or `(dn)` without replacing the whole prompt.

If you still prefer sourced helpers:

```bash
source run_scripts/fikore_ns.sh
fikorens_open ue1
fikorens_exec dn iperf3 -s -p 5202
```

When sourced, `fikorens ue1` keeps its original "replace the current shell" behavior.

### 3. Start the Dashboard

Typical host-side dashboard command:

```bash
run_scripts/run_api.sh
```

Make sure the `.ini` already points to the correct address for your backend.

### 4. Launch FikoRE

```bash
run_scripts/run_emulated.sh config/emulated_rural_n78_single_with_background.ini
```

Equivalent direct lab invocation:

```bash
sudo CONFIG_FILE="$PWD/config/emulated_rural_n78_single_with_background.ini" \
  run_scripts/run_fikore_nfqueue_lab.sh run
```

Behavior:

- if no lab exists yet, `run` creates it first using current defaults or supplied `BACKEND` / `UE_COUNT`;
- if a valid lab already exists, `run` reuses it and does not recreate namespaces;
- if saved state exists but the topology is broken, `run` fails and asks you to run `up`.

### 5. Run Traffic Tests

Assumed addresses:

- DN IP: `10.255.0.2`
- UE1 IP: `10.201.1.2`
- UE2 IP: `10.201.2.2`

#### TCP iperf3

In `dn`:

```bash
iperf3 -s -p 5202
```

In `ue1`:

```bash
iperf3 -c 10.255.0.2 -p 5202 -t 60
```

Downlink:

```bash
iperf3 -c 10.255.0.2 -p 5202 -t 60 -R
```

#### UDP iperf3

In `dn`:

```bash
iperf3 -s -p 5202
```

In `ue1`:

```bash
iperf3 -c 10.255.0.2 -u -b 20M -l 1200 -p 5202 -t 60
```

Downlink:

```bash
iperf3 -c 10.255.0.2 -u -b 20M -l 1200 -p 5202 -t 60 -R
```

#### UDP Prague

In `dn`:

```bash
udp_prague_receiver -a 0.0.0.0 -p 8080
```

In `ue1`:

```bash
udp_prague_sender -c -a 10.255.0.2 -p 8080 -b 20000
```

#### SCReAM

In `dn`:

```bash
scream_bw_test_rx 10.201.1.2 30122
```

In `ue1`:

```bash
scream_bw_test_tx -ect 1 -time 60 10.255.0.2 30122
```

### 6. Generate Offline Visualizations

After the run:

```bash
run_scripts/run_offline.sh plots
```

The analyzers read the newest log directory under `logs/` and write figures under `results/<latest-log>/`.

### 7. Optional PCAP Capture

Start:

```bash
sudo CAPTURE_ID=example CAPTURE_UDP_PORT=5202 run_scripts/run_fikore_nfqueue_lab.sh capture-start
```

Stop and summarize:

```bash
sudo CAPTURE_ID=example run_scripts/run_fikore_nfqueue_lab.sh capture-stop
sudo CAPTURE_ID=example run_scripts/run_fikore_nfqueue_lab.sh capture-summary
```

Artifacts are written under `run_scripts/.captures/example/`.

## Main Actions

Create or recreate topology:

```bash
sudo BACKEND=host UE_COUNT=2 run_scripts/run_fikore_nfqueue_lab.sh up
sudo BACKEND=docker-host UE_COUNT=2 run_scripts/run_fikore_nfqueue_lab.sh up
sudo BACKEND=docker-none UE_COUNT=2 run_scripts/run_fikore_nfqueue_lab.sh up
```

Reuse current topology:

```bash
sudo CONFIG_FILE="$PWD/config/emulated_rural_n78_single_with_background.ini" run_scripts/run_fikore_nfqueue_lab.sh run
sudo run_scripts/run_fikore_nfqueue_lab.sh status
sudo run_scripts/run_fikore_nfqueue_lab.sh shell ue1
sudo run_scripts/run_fikore_nfqueue_lab.sh exec dn -- iperf3 -s -p 5202
sudo run_scripts/run_fikore_nfqueue_lab.sh capture-start
sudo run_scripts/run_fikore_nfqueue_lab.sh capture-stop
sudo run_scripts/run_fikore_nfqueue_lab.sh capture-summary
sudo run_scripts/run_fikore_nfqueue_lab.sh down
```

Lab-only UE lifecycle:

```bash
sudo run_scripts/run_fikore_nfqueue_lab.sh ue-add 3
sudo run_scripts/run_fikore_nfqueue_lab.sh ue-del 3
```

These commands only change lab topology:

- create or remove `fkueN`;
- create or remove its `veth` pair and routing;
- add or remove the corresponding UL/DL `NFQUEUE` rules;
- update saved lab state.

They do not modify the `.ini`. If the config does not define a matching real UE and queue pair, FikoRE will ignore that extra namespace.

## Useful Variables

- `BACKEND=host|docker-host|docker-none`
- `CONFIG_FILE=/path/to/config.ini`
- `UE_COUNT=2`
- `QUEUE_BASE=100`
- `QUEUE_STEP=10`
- `CONTAINER_NAME=fikore-emu`
- `CONTAINER_WORKDIR=/usr/src/5g-network-emulator`
- `CAPTURE_ID=default`
- `CAPTURE_UDP_PORT=9999`
- `CAPTURE_DN_NAMESPACE=1`
- `CAPTURE_DIR=/path/to/run_scripts/.captures`
- `TARGET_UID=<uid>`
- `TARGET_GID=<gid>`
- `TARGET_USER=<user>`
- `TARGET_HOME=/home/<user>`

## Notes

- `emu` is only a valid namespace target in `docker-none`.
- `shell` and `exec` use `root` only to cross into the namespace, then drop back to the original user from the `sudo` session.
- If you invoke the script directly as `root` without `sudo`, set `TARGET_UID`, `TARGET_GID`, `TARGET_USER`, and `TARGET_HOME` explicitly so commands can run as a normal user once inside the namespace.
