#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source "${ROOT_DIR}/run_scripts/python_env.sh"
DEFAULT_CONFIG_FILE="${ROOT_DIR}/config/offline_uma_n78_pedestrian.ini"
CONFIG_FILE="${CONFIG_FILE:-}"
DRAW_MAC="${ROOT_DIR}/py_analyzers/draw_mac.py"
DRAW_UE="${ROOT_DIR}/py_analyzers/draw_ue.py"
REPORT_TEMPLATE="${ROOT_DIR}/run_scripts/templates/offline_report.md"
REPORT_TEMPLATE_EXTENDED="${ROOT_DIR}/run_scripts/templates/offline_report_extended.md"
ALL_PLOTS=0

resolve_path() {
    local path="$1"
    if command -v realpath >/dev/null 2>&1; then
        realpath "${path}"
    else
        readlink -f "${path}"
    fi
}

usage() {
    cat <<EOF
Usage: $(basename "$0") [-a] [config.ini]
       $(basename "$0") plots [-a]

Environment:
  CONFIG_FILE=/path/to/config.ini  # optional alternative to the positional config path

Examples:
  $(basename "$0")
  $(basename "$0") -a
  $(basename "$0") config/offline_umi_n40_npn.ini
  $(basename "$0") -a config/offline_umi_n40_npn.ini
  CONFIG_FILE=$ROOT_DIR/config/offline_rural_n78_vehicular.ini $(basename "$0")
  $(basename "$0") plots
EOF
}

generate_plots() {
    activate_fikore_python
    echo "[INFO] Generating offline plots from the latest log directory..."
    if ! fikore_python "${DRAW_MAC}"; then
        echo "[WARN] Skipping MAC plots. Check optional Python dependencies (for example matplotlib)." >&2
        return 0
    fi
    local ue_args=()
    if [[ "${ALL_PLOTS}" -eq 1 ]]; then
        ue_args+=("-a")
    fi
    ue_args+=("--config" "${CONFIG_FILE}")
    if ! fikore_python "${DRAW_UE}" "${ue_args[@]}"; then
        echo "[WARN] Skipping UE plots. Check optional Python dependencies (for example matplotlib)." >&2
        return 0
    fi
    write_report_template
    echo "[DONE] Offline plots generated under results/<latest-log>/."
}

get_latest_results_dir() {
    local latest_results_dir
    local timestamp_results=()
    local result_dir
    for result_dir in "${ROOT_DIR}"/results/*; do
        [[ -d "${result_dir}" ]] || continue
        if [[ "$(basename "${result_dir}")" =~ ^[0-9]{4}_[0-9]{2}_[0-9]{2}_[0-9]{2}_[0-9]{2}_[0-9]{2}$ ]]; then
            timestamp_results+=("${result_dir}")
        fi
    done

    if [[ ${#timestamp_results[@]} -gt 0 ]]; then
        IFS=$'\n' read -r -d '' -a timestamp_results < <(printf '%s\n' "${timestamp_results[@]}" | sort && printf '\0')
        latest_results_dir="${timestamp_results[${#timestamp_results[@]}-1]}"
    else
        latest_results_dir="$(find "${ROOT_DIR}/results" -mindepth 1 -maxdepth 1 -type d -printf '%T@ %p\n' 2>/dev/null | sort -nr | head -n 1 | cut -d' ' -f2-)"
    fi
    [[ -n "${latest_results_dir}" ]] || return 1
    printf '%s\n' "${latest_results_dir}"
}

write_report_template() {
    local latest_results_dir
    latest_results_dir="$(get_latest_results_dir)" || {
        echo "[WARN] Could not find the latest results directory to place the Markdown report." >&2
        return 0
    }

    local template_file="${REPORT_TEMPLATE}"
    if [[ "${ALL_PLOTS}" -eq 1 ]]; then
        template_file="${REPORT_TEMPLATE_EXTENDED}"
    fi

    cp "${template_file}" "${latest_results_dir}/README.md"
    echo "[INFO] Wrote report template to ${latest_results_dir}/README.md"
}

copy_run_config() {
    local latest_results_dir
    latest_results_dir="$(get_latest_results_dir)" || {
        echo "[WARN] Could not find the latest results directory to copy the config file." >&2
        return 0
    }

    cp "${CONFIG_FILE}" "${latest_results_dir}/config.ini"
    echo "[INFO] Copied config file to ${latest_results_dir}/config.ini"
}

ACTION="run"
POSITIONAL=()

while (($#)); do
    case "$1" in
        -a|--all)
            ALL_PLOTS=1
            shift
            ;;
        plots)
            ACTION="plots"
            shift
            ;;
        run)
            ACTION="run"
            shift
            ;;
        -h|--help|help)
            usage
            exit 0
            ;;
        *)
            POSITIONAL+=("$1")
            shift
            ;;
    esac
done

if [[ ${#POSITIONAL[@]} -gt 1 ]]; then
    usage
    exit 1
fi

if [[ ${#POSITIONAL[@]} -eq 1 ]]; then
    if [[ -z "${CONFIG_FILE}" ]]; then
        CONFIG_FILE="${POSITIONAL[0]}"
    else
        usage
        exit 1
    fi
fi

if [[ "${ACTION}" == "run" && -z "${CONFIG_FILE}" ]]; then
    CONFIG_FILE="${DEFAULT_CONFIG_FILE}"
fi

if [[ "${ACTION}" == "run" ]]; then
    [[ -f "${CONFIG_FILE}" ]] || {
        echo "[ERROR] Config file does not exist: ${CONFIG_FILE}" >&2
        exit 1
    }
    CONFIG_FILE="$(resolve_path "${CONFIG_FILE}")"
fi

case "${ACTION}" in
    run)
        cd "${ROOT_DIR}"
        echo "[RUN] Running offline simulation with $(basename "${CONFIG_FILE}")"
        ./bin/fikore "${CONFIG_FILE}"
        generate_plots
        copy_run_config
        ;;
    plots)
        generate_plots
        ;;
    *)
        usage
        exit 1
        ;;
esac
