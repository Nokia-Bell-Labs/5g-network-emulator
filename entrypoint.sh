#!/usr/bin/env bash
# Copyright 2026 Nokia
# Licensed under the BSD 3-Clause Clear License
# SPDX-License-Identifier: BSD-3-Clause-Clear
#
# Starts the emulator and, by default, the control API next to it.
#
#   docker run ... <config.ini>          emulator + API
#   docker run ... --no-api <config.ini> emulator only
#   docker run ... --api-only            API only (emulator in another container)
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WITH_API=1
API_ONLY=0
ARGS=()

for arg in "$@"; do
  case "$arg" in
    --no-api)   WITH_API=0 ;;
    --api-only) API_ONLY=1 ;;
    *)          ARGS+=("$arg") ;;
  esac
done

API_PID=""
cleanup() {
  [ -n "${API_PID}" ] && kill "${API_PID}" 2>/dev/null || true
}
trap cleanup EXIT INT TERM

if [ "${WITH_API}" -eq 1 ]; then
  FIKORE_API_HOST="${FIKORE_API_HOST:-0.0.0.0}" "${ROOT_DIR}/run_scripts/run_api.sh" &
  API_PID=$!
  echo "[entrypoint] API on ${FIKORE_API_HOST:-0.0.0.0}:${FIKORE_API_PORT:-8100} (pid ${API_PID})"
fi

if [ "${API_ONLY}" -eq 1 ]; then
  wait "${API_PID}"
  exit $?
fi

exec "${ROOT_DIR}/bin/fikore" "${ARGS[@]}"
