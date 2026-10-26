#!/usr/bin/env bash
# Copyright 2026 Nokia
# Licensed under the BSD 3-Clause Clear License
# SPDX-License-Identifier: BSD-3-Clause-Clear
#
# Starts the REST/WebSocket API of the control plane.
#
# The API venv is separate from the repo's .venv, which belongs to the analyzers: the
# API is deployed with the emulator and must not drag matplotlib or numpy along.
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
API_DIR="${ROOT_DIR}/api"
VENV_DIR="${API_DIR}/.venv"

HOST="${FIKORE_API_HOST:-127.0.0.1}"
PORT="${FIKORE_API_PORT:-8100}"

if [ ! -x "${VENV_DIR}/bin/python" ]; then
  echo "[run_api] creating ${VENV_DIR}"
  python3 -m venv "${VENV_DIR}"
  "${VENV_DIR}/bin/pip" install --quiet --upgrade pip
  "${VENV_DIR}/bin/pip" install --quiet -r "${API_DIR}/requirements.txt"
fi

cd "${API_DIR}"
exec "${VENV_DIR}/bin/python" -m uvicorn fikore_api.main:app --host "${HOST}" --port "${PORT}" "$@"
