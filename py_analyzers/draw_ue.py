#!/usr/bin/env python3
import argparse
import json
import os
import re
import sys
import numpy as np
import matplotlib.pyplot as plt
from matplotlib.colors import ListedColormap
from glob import glob

TIMESTAMP_DIR_RE = re.compile(r"^\d{4}_\d{2}_\d{2}_\d{2}_\d{2}_\d{2}$")


def find_ue_logs(base_path):
    timestamp_ue_dirs = []
    fallback_ue_dirs = []
    for ue_dir in glob(os.path.join(base_path, '*', 'ue')):
        log_dir = os.path.basename(os.path.dirname(ue_dir))
        if TIMESTAMP_DIR_RE.fullmatch(log_dir):
            timestamp_ue_dirs.append(ue_dir)
        else:
            fallback_ue_dirs.append(ue_dir)

    ue_dirs = sorted(timestamp_ue_dirs) if timestamp_ue_dirs else fallback_ue_dirs
    if not ue_dirs:
        print(f"[ERROR] No UE log directories found in {base_path}")
        sys.exit(1)
    latest_dir = ue_dirs[-1] if timestamp_ue_dirs else max(ue_dirs, key=os.path.getmtime)
    log_files = sorted(glob(os.path.join(latest_dir, 'ue_log_*.txt')))
    return latest_dir, log_files


def parse_log_file_with_tx(file_path):
    data = {
        'x': [], 'y': [],
        'rsrp_ul': [], 'rsrp_dl': [],
        'sinr_ul': [], 'sinr_dl': [],
        'rul': [], 'rdl': [], 'ri': [],
        'gul': [], 'gdl': [], 'eul': [], 'edl': [],
        'ceul': [], 'cedl': [], 'cebul': [], 'cebdl': [],
        'aqmdul': [], 'aqmddl': [], 'aqmdbul': [], 'aqmdbdl': [],
        'nfqrecvul': [], 'nfqrecvdl': [], 'nfqrlsul': [], 'nfqrlsdl': [],
        'nfqcewul': [], 'nfqcewdl': [],
        'nfqdropul': [], 'nfqdropdl': [], 'nfqrfailul': [], 'nfqrfaildl': [],
        'nfqsfailul': [], 'nfqsfaildl': [],
        'qlul': [], 'qldl': [], 'qcul': [], 'qcdl': [],
        'plul': [], 'pldl': [], 'pcul': [], 'pcdl': [], 'pclul': [], 'pcldl': []
    }

    with open(file_path, 'r') as f:
        tx_flag = None
        for line in f:
            tokens = line.split()
            temp = {}
            for token in tokens:
                if ':' in token:
                    k, v = token.split(':', 1)
                    try:
                        temp[k] = float(v)
                        if k not in data:
                            data[k] = []
                    except ValueError:
                        pass
            if 'tx' in temp:
                tx_flag = int(temp['tx'])
            if 'sinr' in temp and tx_flag is not None:
                if tx_flag == 1:
                    data['sinr_ul'].append(temp['sinr'])
                else:
                    data['sinr_dl'].append(temp['sinr'])
            if 'rsrp' in temp and tx_flag is not None:
                if tx_flag == 1:
                    data['rsrp_ul'].append(temp['rsrp'])
                else:
                    data['rsrp_dl'].append(temp['rsrp'])
            for key in ['x', 'y', 'rul', 'rdl', 'ri', 'gul', 'gdl', 'eul', 'edl',
                        'ceul', 'cedl', 'cebul', 'cebdl',
                        'aqmdul', 'aqmddl', 'aqmdbul', 'aqmdbdl',
                        'nfqrecvul', 'nfqrecvdl', 'nfqrlsul', 'nfqrlsdl',
                        'nfqcewul', 'nfqcewdl',
                        'nfqdropul', 'nfqdropdl', 'nfqrfailul', 'nfqrfaildl',
                        'nfqsfailul', 'nfqsfaildl',
                        'qlul', 'qldl', 'qcul', 'qcdl',
                        'plul', 'pldl', 'pcul', 'pcdl', 'pclul', 'pcldl']:
                if key in temp:
                    data[key].append(temp[key])
            for key, value in temp.items():
                if key not in ('tx', 'sinr', 'rsrp') and key not in ['x', 'y', 'rul', 'rdl', 'ri', 'gul', 'gdl', 'eul', 'edl',
                                                                      'ceul', 'cedl', 'cebul', 'cebdl',
                                                                      'aqmdul', 'aqmddl', 'aqmdbul', 'aqmdbdl',
                                                                      'nfqrecvul', 'nfqrecvdl', 'nfqrlsul', 'nfqrlsdl',
                                                                      'nfqcewul', 'nfqcewdl',
                                                                      'nfqdropul', 'nfqdropdl', 'nfqrfailul', 'nfqrfaildl',
                                                                      'nfqsfailul', 'nfqsfaildl',
                                                                      'qlul', 'qldl', 'qcul', 'qcdl',
                                                                      'plul', 'pldl', 'pcul', 'pcdl', 'pclul', 'pcldl']:
                    data[key].append(value)
    return data


def map_path_from_config(config_path):
    in_scenario = False
    with open(config_path, 'r') as config:
        for line in config:
            line = line.strip()
            if line.startswith('[') and line.endswith(']'):
                in_scenario = line == '[Scenario]'
                continue
            if in_scenario and line.startswith('map_file:'):
                map_path = line.split(':', 1)[1].strip()
                return map_path if os.path.isabs(map_path) else os.path.abspath(map_path)
    raise ValueError(f"No map_file found in [Scenario] of {config_path}")


def load_fading_map(config_path):
    with open(map_path_from_config(config_path), 'r') as map_handle:
        fading_map = json.load(map_handle)
    values = np.asarray(fading_map['map'], dtype=float)
    if values.ndim != 2 or values.size == 0:
        raise ValueError("Fading map has no two-dimensional data")
    cell_size = float(fading_map['cell_size'])
    return values, cell_size


def dashboard_fading_cmap():
    values = np.linspace(0.0, 1.0, 256)
    colours = np.column_stack((
        np.clip(1.6 * values - 0.3, 0.0, 1.0),
        np.clip(1.4 * values - 0.1, 0.0, 1.0),
        np.clip(0.9 - 1.1 * values, 0.0, 1.0),
    ))
    return ListedColormap(colours)


def plot_combined_trajectory(all_data, outdir, config_path):
    plt.figure(figsize=(10, 10))
    axis = plt.gca()
    fading_map, cell_size = load_fading_map(config_path)
    side = cell_size * fading_map.shape[0]
    low, high = np.percentile(fading_map, (1, 99))
    image = axis.imshow(
        fading_map,
        extent=(-side / 2, side / 2, -side / 2, side / 2),
        origin='upper',
        cmap=dashboard_fading_cmap(),
        vmin=low,
        vmax=high,
        alpha=0.45,
    )
    plt.colorbar(image, ax=axis, label='Fading map (dB, 1st–99th percentile)')
    cmap = plt.get_cmap("tab10")
    for i, data in enumerate(all_data):
        x, y = data['x'], data['y']
        n = min(len(x), len(y)) # Trim to the shortest length
        x, y = x[:n], y[:n]
        if not x or not y:
            continue
        plt.plot(x, y, label=f"UE {i}", alpha=0.6, color=cmap(i % cmap.N))
        plt.plot(x[0], y[0], 'o', color=cmap(i % cmap.N))
        plt.plot(x[-1], y[-1], 's', color=cmap(i % cmap.N))
    plt.plot(0, 0, 'kp', label='Base Station', markersize=10)
    plt.axis('equal')
    plt.xlabel('x (m)')
    plt.ylabel('y (m)')
    plt.grid(True)
    plt.legend()
    plt.title("UE Trajectories Over Fading Map")
    plt.tight_layout()
    path = os.path.join(outdir, "trajectory.png")
    plt.savefig(path)
    print(f"[SAVED] {path}")


def plot_histograms_ul_dl(all_data, key_ul, key_dl, title, xlabel, outname, bins, outdir):
    fig, axs = plt.subplots(2, 1, figsize=(10, 10), sharex=True)
    cmap = plt.get_cmap("tab10")
    for i, data in enumerate(all_data):
        ul_vals = np.asarray(data[key_ul], dtype=float)
        dl_vals = np.asarray(data[key_dl], dtype=float)

        ul_vals = ul_vals[np.isfinite(ul_vals)]
        dl_vals = dl_vals[np.isfinite(dl_vals)]

        if ul_vals.size:
            axs[0].hist(ul_vals, bins=bins, alpha=0.5, label=f"UE {i}", color=cmap(i % cmap.N), density=True, edgecolor='black')
        if dl_vals.size:
            axs[1].hist(dl_vals, bins=bins, alpha=0.5, label=f"UE {i}", color=cmap(i % cmap.N), density=True, edgecolor='black')
    axs[0].set_title(f"{title} (UL)")
    axs[1].set_title(f"{title} (DL)")
    axs[1].set_xlabel(xlabel)
    axs[0].legend()
    axs[1].legend()
    axs[0].grid(True)
    axs[1].grid(True)
    fig.tight_layout()
    path = os.path.join(outdir, outname)
    plt.savefig(path)
    print(f"[SAVED] {path}")


def plot_histograms_per_ue(all_data, key, title, xlabel, outname, outdir, bins):
    plt.figure(figsize=(10, 6))
    cmap = plt.get_cmap("tab10")
    for i, data in enumerate(all_data):
        vals = np.asarray(data[key], dtype=float)
        vals = vals[np.isfinite(vals)]
        if not vals.size:
            continue
        plt.hist(vals, bins=bins, alpha=0.5, label=f"UE {i}", density=True, edgecolor='black', color=cmap(i % cmap.N))
    plt.title(title)
    plt.xlabel(xlabel)
    plt.grid(True)
    plt.legend()
    plt.tight_layout()
    path = os.path.join(outdir, outname)
    plt.savefig(path)
    print(f"[SAVED] {path}")


def plot_l4s_series(all_data, outdir):
    series = [
        ('ceul', 'cedl', 'CE Marks', 'Packets / log interval', 'ce_marks_ul_dl.png'),
        ('aqmdul', 'aqmddl', 'AQM Drops', 'Packets / log interval', 'aqm_drops_ul_dl.png'),
        ('qlul', 'qldl', 'L4S Queue Size', 'Packets', 'l4s_queue_size_ul_dl.png'),
        ('qcul', 'qcdl', 'Classic Queue Size', 'Packets', 'classic_queue_size_ul_dl.png'),
        ('plul', 'pldl', 'DualPI2 p_L', 'Probability', 'dualpi2_pl_ul_dl.png'),
        ('pcul', 'pcdl', 'DualPI2 p_C', 'Probability', 'dualpi2_pc_ul_dl.png'),
        ('pclul', 'pcldl', 'DualPI2 p_CL', 'Probability', 'dualpi2_pcl_ul_dl.png'),
    ]
    cmap = plt.get_cmap("tab10")
    for key_ul, key_dl, title, ylabel, outname in series:
        if not any(len(data.get(key_ul, [])) or len(data.get(key_dl, [])) for data in all_data):
            continue
        fig, axs = plt.subplots(2, 1, figsize=(12, 8), sharex=True)
        for i, data in enumerate(all_data):
            ul_vals = np.asarray(data.get(key_ul, []), dtype=float)
            dl_vals = np.asarray(data.get(key_dl, []), dtype=float)
            if ul_vals.size:
                axs[0].plot(np.arange(ul_vals.size), ul_vals, label=f"UE {i}", color=cmap(i % cmap.N), alpha=0.8)
            if dl_vals.size:
                axs[1].plot(np.arange(dl_vals.size), dl_vals, label=f"UE {i}", color=cmap(i % cmap.N), alpha=0.8)
        axs[0].set_title(f"{title} (UL)")
        axs[1].set_title(f"{title} (DL)")
        axs[0].set_ylabel(ylabel)
        axs[1].set_ylabel(ylabel)
        axs[1].set_xlabel("Log sample")
        axs[0].grid(True)
        axs[1].grid(True)
        axs[0].legend()
        axs[1].legend()
        fig.tight_layout()
        path = os.path.join(outdir, outname)
        plt.savefig(path)
        print(f"[SAVED] {path}")


def plot_drop_sources(all_data, outdir):
    series_groups = [
        (
            'UL Drop Sources',
            [
                ('eul', 'Error rate [Mbps]'),
                ('aqmdul', 'AQM drops [pkts]'),
                ('aqmdbul', 'AQM dropped bits'),
                ('nfqdropul', 'NFQUEUE dropped pkts'),
                ('nfqrfailul', 'NFQUEUE recv fail events'),
                ('nfqsfailul', 'NFQUEUE release fail events'),
                ('ceul', 'CE marks [pkts]'),
            ],
            'drop_sources_ul.png',
        ),
        (
            'DL Drop Sources',
            [
                ('edl', 'Error rate [Mbps]'),
                ('aqmddl', 'AQM drops [pkts]'),
                ('aqmdbdl', 'AQM dropped bits'),
                ('nfqdropdl', 'NFQUEUE dropped pkts'),
                ('nfqrfaildl', 'NFQUEUE recv fail events'),
                ('nfqsfaildl', 'NFQUEUE release fail events'),
                ('cedl', 'CE marks [pkts]'),
            ],
            'drop_sources_dl.png',
        ),
    ]

    cmap = plt.get_cmap("tab10")
    for title, key_specs, outname in series_groups:
        present_specs = [spec for spec in key_specs if any(len(data.get(spec[0], [])) for data in all_data)]
        if not present_specs:
            continue

        fig, axs = plt.subplots(len(present_specs), 1, figsize=(12, 2.6 * len(present_specs)), sharex=True)
        if len(present_specs) == 1:
            axs = [axs]

        for ax, (key, ylabel) in zip(axs, present_specs):
            for i, data in enumerate(all_data):
                vals = np.asarray(data.get(key, []), dtype=float)
                vals = vals[np.isfinite(vals)]
                if not vals.size:
                    continue
                ax.plot(np.arange(vals.size), vals, label=f"UE {i}", color=cmap(i % cmap.N), alpha=0.8)
            ax.set_ylabel(ylabel)
            ax.grid(True)
            ax.legend()

        axs[0].set_title(title)
        axs[-1].set_xlabel("Log sample")
        fig.tight_layout()
        path = os.path.join(outdir, outname)
        plt.savefig(path)
        print(f"[SAVED] {path}")


def plot_nfqueue_series(all_data, outdir):
    nfqueue_keys = sorted({
        key for data in all_data for key in data.keys()
        if key.startswith('nfqueue_') or key.startswith('nfq')
    })
    if not nfqueue_keys:
        return

    present_keys = [key for key in nfqueue_keys if any(len(data.get(key, [])) for data in all_data)]
    if not present_keys:
        return

    fig, axs = plt.subplots(len(present_keys), 1, figsize=(12, 2.6 * len(present_keys)), sharex=True)
    if len(present_keys) == 1:
        axs = [axs]

    cmap = plt.get_cmap("tab10")
    for ax, key in zip(axs, present_keys):
        for i, data in enumerate(all_data):
            vals = np.asarray(data.get(key, []), dtype=float)
            vals = vals[np.isfinite(vals)]
            if not vals.size:
                continue
            ax.plot(np.arange(vals.size), vals, label=f"UE {i}", color=cmap(i % cmap.N), alpha=0.8)
        ax.set_ylabel(key)
        ax.grid(True)
        ax.legend()

    axs[0].set_title("NFQUEUE Results")
    axs[-1].set_xlabel("Log sample")
    fig.tight_layout()
    path = os.path.join(outdir, "nfqueue_results.png")
    plt.savefig(path)
    print(f"[SAVED] {path}")


def parse_args():
    parser = argparse.ArgumentParser(
        description="Generate UE plots from the latest offline log directory."
    )
    parser.add_argument(
        "-a",
        "--all",
        action="store_true",
        help="include DualQ/L4S/NFQUEUE-specific plots in addition to the general ones",
    )
    parser.add_argument(
        "--config",
        required=True,
        help="INI file used for the simulation",
    )
    return parser.parse_args()


def main():
    args = parse_args()
    script_dir = os.path.dirname(os.path.realpath(sys.argv[0]))
    logs_base = os.path.join(script_dir, '..', 'logs')
    ue_dir, log_files = find_ue_logs(logs_base)
    log_folder = os.path.basename(os.path.dirname(ue_dir))
    out_dir = os.path.join(script_dir, "..", "results", log_folder, "ue")
    os.makedirs(out_dir, exist_ok=True)

    all_data = [parse_log_file_with_tx(f) for f in log_files]
    plot_combined_trajectory(all_data, out_dir, args.config)

    plot_histograms_ul_dl(all_data, 'sinr_ul', 'sinr_dl', 'SINR Histogram', 'SINR (dB)', 'sinr_hist_ul_dl.png', bins=40, outdir=out_dir)
    plot_histograms_ul_dl(all_data, 'rsrp_ul', 'rsrp_dl', 'RSRP Histogram', 'RSRP (dBm)', 'rsrp_hist_ul_dl.png', bins=40, outdir=out_dir)
    plot_histograms_per_ue(all_data, 'rul', 'UL Throughput Histogram', 'Throughput (Mbps)', 'rul_hist.png', outdir=out_dir, bins=30)
    plot_histograms_per_ue(all_data, 'rdl', 'DL Throughput Histogram', 'Throughput (Mbps)', 'rdl_hist.png', outdir=out_dir, bins=30)
    plot_histograms_per_ue(all_data, 'ri', 'Rank Indicator Histogram', 'Rank', 'ri_hist.png', outdir=out_dir, bins=np.arange(0.5, 5.5, 1))
    plot_drop_sources(all_data, out_dir)
    if args.all:
        plot_histograms_per_ue(all_data, 'ceul', 'UL CE Marks Histogram', 'CE marks / log interval', 'ceul_hist.png', outdir=out_dir, bins=30)
        plot_histograms_per_ue(all_data, 'cedl', 'DL CE Marks Histogram', 'CE marks / log interval', 'cedl_hist.png', outdir=out_dir, bins=30)
        plot_histograms_per_ue(all_data, 'aqmdul', 'UL AQM Drops Histogram', 'Drops / log interval', 'aqmdul_hist.png', outdir=out_dir, bins=30)
        plot_histograms_per_ue(all_data, 'aqmddl', 'DL AQM Drops Histogram', 'Drops / log interval', 'aqmddl_hist.png', outdir=out_dir, bins=30)
        plot_l4s_series(all_data, out_dir)
        plot_nfqueue_series(all_data, out_dir)


if __name__ == "__main__":
    main()
