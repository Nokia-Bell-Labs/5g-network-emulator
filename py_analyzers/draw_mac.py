#!/usr/bin/env python3
import sys
import os
import glob
import re
import numpy as np
import matplotlib.pyplot as plt

TIMESTAMP_DIR_RE = re.compile(r"^\d{4}_\d{2}_\d{2}_\d{2}_\d{2}_\d{2}$")
UE_LOG_RE = re.compile(r"ue_log_(\d+)\.txt$")

# Capacity reference over the throughput curves. Off: the model behind it is a rough
# Shannon bound over one UE's SINR and it is not calibrated. Set to True to debug with it.
DRAW_SHANNON_LIMIT = False

class ShannonParams:
    def __init__(self, bandwidth_mhz, mimo_layers=1, bw_efficiency=0.67,
                 sinr_offset_db=1.0, ul_divisor=2, sinr_sample_rate=100):
        self.bandwidth_mhz = bandwidth_mhz
        self.mimo_layers = mimo_layers
        self.bw_efficiency = bw_efficiency
        self.sinr_offset_db = sinr_offset_db
        self.ul_divisor = ul_divisor
        self.sinr_sample_rate = sinr_sample_rate

    def compute_capacity(self, sinr_db: np.ndarray, is_ul: bool = False) -> np.ndarray:
        adjusted_sinr = 10 ** ((sinr_db - self.sinr_offset_db) / 10.0)
        capacity = self.mimo_layers * self.bandwidth_mhz * self.bw_efficiency * np.log2(1 + adjusted_sinr)
        if is_ul:
            capacity /= self.ul_divisor
        return capacity

def latest_log_dir(base_path: str) -> str:
    dirs = [d for d in os.listdir(base_path) if os.path.isdir(os.path.join(base_path, d))]
    if not dirs:
        print(f"[ERROR] No log directories found in {base_path}")
        sys.exit(1)
    timestamp_dirs = sorted(d for d in dirs if TIMESTAMP_DIR_RE.fullmatch(d))
    if timestamp_dirs:
        return os.path.join(base_path, timestamp_dirs[-1])
    dirs.sort(key=lambda d: os.path.getmtime(os.path.join(base_path, d)), reverse=True)
    return os.path.join(base_path, dirs[0])

def parse_grid_log(fname: str) -> dict[int, dict[int, float]]:
    data = {}
    with open(fname, "r") as f:
        for line in f:
            tokens = [t for t in line.strip().split() if ":" in t]
            entry = {}
            for token in tokens:
                k, v = token.split(":", 1)
                try:
                    entry[k] = float(v)
                except ValueError:
                    entry[k] = v
            if {"ts", "id", "tp"} <= entry.keys():
                ue = int(entry["id"])
                sec = int(float(entry["ts"]))
                tp = entry["tp"]
                data.setdefault(ue, {}).setdefault(sec, 0.0)
                data[ue][sec] += tp
    return data

def parse_grid_header(fname: str) -> dict[str, str]:
    with open(fname, "r") as f:
        for line in f:
            entry = {}
            for token in line.strip().split():
                if ":" in token:
                    k, v = token.split(":", 1)
                    entry[k] = v
            if "id" in entry:
                break
            if "bw" in entry:
                return entry
    return {}

def shannon_params(header: dict[str, str], direction: str) -> ShannonParams | None:
    try:
        bw_mhz = float(header.get("bw", 0.0)) / 1e6
    except ValueError:
        bw_mhz = 0.0
    if bw_mhz <= 0:
        return None

    # The carrier is shared in time, so each direction only gets its slots.
    share = 1.0
    tdd = header.get("tdd", "").split("/")
    if len(tdd) >= 2:
        try:
            n_dl, n_ul = float(tdd[0]), float(tdd[1])
            if n_dl + n_ul > 0:
                share = (n_dl if direction == "DL" else n_ul) / (n_dl + n_ul)
        except ValueError:
            pass
    return ShannonParams(bandwidth_mhz=bw_mhz * share)

def parse_sinr(fname: str) -> np.ndarray:
    sinr_vals = []
    with open(fname, "r") as f:
        for token in f.read().split():
            if token.startswith("sinr:"):
                try:
                    sinr_vals.append(float(token.split(":", 1)[1]))
                except ValueError:
                    pass
    return np.asarray(sinr_vals)

def find_reference_ue_log(ue_dir: str) -> str | None:
    # UE 0 is the UE under study, so it is the reference for the capacity curve.
    files = glob.glob(os.path.join(ue_dir, "ue_log_*.txt"))
    if not files:
        return None
    for f in files:
        match = UE_LOG_RE.search(os.path.basename(f))
        if match and int(match.group(1)) == 0:
            return f
    files.sort(key=os.path.getmtime, reverse=True)
    return files[0]

def draw_time_series(output_dir: str, grid_file: str, ue_log: str | None, direction: str):
    grid_data = parse_grid_log(grid_file)
    SAMPLE_FREQ = 10
    K2M = 1e-3

    ue_lines = {}
    for ue, sec_dict in grid_data.items():
        secs = sorted(sec_dict.keys())
        mbps = [(sec_dict[s] / SAMPLE_FREQ) * K2M for s in secs]
        ue_lines[ue] = (secs, mbps)

    capacity_vals, capacity_secs = [], []
    capacity_label = "Modified Shannon Limit"
    if DRAW_SHANNON_LIMIT:
        shannon_cfg = shannon_params(parse_grid_header(grid_file), direction)
        sinr_all = parse_sinr(ue_log) if ue_log else np.asarray([])
        if shannon_cfg is None or not sinr_all.size:
            # Without the grid bandwidth or a SINR series the reference would be invented.
            print(f"[WARN] {direction}: no capacity reference, "
                  f"{'no bandwidth in the grid log' if shannon_cfg is None else 'no SINR samples'}.")
        else:
            match = UE_LOG_RE.search(os.path.basename(ue_log))
            if match:
                capacity_label += f" (UE{match.group(1)})"
            sinr_dir = sinr_all[0::2] if direction == "DL" else sinr_all[1::2]
            C_samples = shannon_cfg.compute_capacity(sinr_dir, is_ul=(direction == "UL"))
            n_seconds = len(C_samples) // shannon_cfg.sinr_sample_rate
            capacity_vals = [np.mean(C_samples[i*shannon_cfg.sinr_sample_rate:(i+1)*shannon_cfg.sinr_sample_rate])
                             for i in range(n_seconds)]
            capacity_secs = list(range(n_seconds))

    plt.figure(figsize=(12, 6))
    cmap = plt.get_cmap("tab10")
    for idx, (ue, (secs, mbps)) in enumerate(ue_lines.items()):
        plt.plot(secs, mbps, linestyle="-", marker="o", linewidth=2,
                 color=cmap(idx % cmap.N), label=f"UE {ue}")
    if capacity_secs:
        plt.plot(capacity_secs, capacity_vals, "k--", linewidth=2, label=capacity_label)

    plt.xlabel("Time (s)")
    plt.ylabel(f"Throughput {direction} (Mbps)")
    plt.title(f"{direction} Throughput vs Time")
    plt.grid(True)
    plt.legend()
    plt.tight_layout()
    out_png = os.path.join(output_dir, f"{direction.lower()}_throughput.png")
    plt.savefig(out_png)
    print(f"[SAVED] {out_png}")

def draw_cdf(output_dir: str, grid_file: str, direction: str = "DL"):
    ue_totals, ue_seconds = {}, {}
    with open(grid_file, "r") as f:
        for line in f:
            tokens = [t for t in line.strip().split() if ":" in t]
            entry = {}
            for token in tokens:
                k, v = token.split(":", 1)
                try:
                    entry[k] = float(v)
                except ValueError:
                    entry[k] = v
            if {"id", "tp", "ts"} <= entry.keys():
                ue = int(entry["id"])
                tp = float(entry["tp"])
                sec = int(float(entry["ts"]))
                ue_totals.setdefault(ue, 0.0)
                ue_seconds.setdefault(ue, set()).add(sec)
                ue_totals[ue] += tp

    ue_avg = {ue: (tp / len(ue_seconds[ue])) * 1e-3 for ue, tp in ue_totals.items() if len(ue_seconds[ue]) > 0}

    if len(ue_avg) == 0:
        print("[WARN] No valid UE data for CDF.")
        return

    norm_avg = np.array(list(ue_avg.values()))
    if len(ue_avg) > 1:
        norm_avg /= (len(ue_avg) * 30)

    sorted_vals = np.sort(norm_avg)
    cdf = np.arange(1, len(sorted_vals) + 1) / len(sorted_vals)

    plt.figure(figsize=(8, 6))
    plt.plot(sorted_vals, cdf, linewidth=2, marker='o')
    plt.xlabel(f"Normalized Average UE Throughput {direction} (Mbps per UE)")
    plt.ylabel("CDF")
    plt.title(f"CDF of Normalized UE Throughput ({direction})")
    plt.grid(True)
    plt.tight_layout()

    suffix = "" if direction == "DL" else f"_{direction.lower()}"
    out_png = os.path.join(output_dir, f"cdf_throughput{suffix}.png")
    plt.savefig(out_png)
    print(f"[SAVED] {out_png}")

def main():
    script_dir = os.path.dirname(os.path.abspath(__file__))
    logs_base = os.path.join(script_dir, "..", "logs")
    latest_log = latest_log_dir(logs_base)
    log_folder = os.path.basename(os.path.normpath(latest_log))

    results_base = os.path.join(script_dir, "..", "results", log_folder, "mac")
    os.makedirs(results_base, exist_ok=True)

    mac_dir = os.path.join(latest_log, "mac")
    ue_dir = os.path.join(latest_log, "ue")
    ue_log = find_reference_ue_log(ue_dir) if os.path.isdir(ue_dir) else None

    ul_file = os.path.join(mac_dir, "grid_log_ul.txt")
    dl_file = os.path.join(mac_dir, "grid_log_dl.txt")

    directions = [(d, f) for d, f in (("DL", dl_file), ("UL", ul_file))
                  if os.path.exists(f) and os.path.getsize(f) > 0]
    if not directions:
        print("[ERROR] UL or DL grid logs not found.")
        sys.exit(1)

    for direction, grid_file in directions:
        draw_time_series(results_base, grid_file, ue_log, direction)
        draw_cdf(results_base, grid_file, direction)

if __name__ == "__main__":
    main()
