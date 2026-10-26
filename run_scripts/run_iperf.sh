#!/usr/bin/env bash
# Copyright 2026 Nokia
# Licensed under the BSD 3-Clause Clear License
# SPDX-License-Identifier: BSD-3-Clause-Clear
#
# Offline iperf-shaped session on top of a FikoRE scenario.
#
# The transport venv is separate from the repo's .venv (analyzers) and from api/.venv.
# Arguments are forwarded to fikore-iperf3. Paths are relative to the current directory.
# Prague is not installed here; build it with `make -C transport/prague` before -C prague.
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TRANSPORT_DIR="${ROOT_DIR}/transport"
VENV_DIR="${TRANSPORT_DIR}/.venv"

if [ ! -x "${VENV_DIR}/bin/fikore-iperf3" ]; then
  echo "[run_iperf] creating ${VENV_DIR}"
  python3 -m venv "${VENV_DIR}"
  "${VENV_DIR}/bin/pip" install --quiet --upgrade pip
  "${VENV_DIR}/bin/pip" install --quiet -e "${TRANSPORT_DIR}"
fi

exec "${VENV_DIR}/bin/fikore-iperf3" "$@"
