# Offline Analysis Report

This report summarizes the full plot set generated for the latest offline FikoRE run, including DualQ/L4S/NFQUEUE-specific figures.

## MAC Plots

### Downlink Throughput vs Time
File: [mac/dl_throughput.png](mac/dl_throughput.png)

Shows the downlink throughput over time for the scheduled UEs.

![Downlink Throughput vs Time](mac/dl_throughput.png)

### Uplink Throughput vs Time
File: [mac/ul_throughput.png](mac/ul_throughput.png)

Shows the uplink throughput over time for the scheduled UEs.

![Uplink Throughput vs Time](mac/ul_throughput.png)

### CDF of Normalized UE Throughput (Downlink)
File: [mac/cdf_throughput.png](mac/cdf_throughput.png)

Shows the cumulative distribution of average UE throughput. It is useful for checking fairness and throughput spread across users.

![CDF of Normalized UE Throughput](mac/cdf_throughput.png)

### CDF of Normalized UE Throughput (Uplink)
File: [mac/cdf_throughput_ul.png](mac/cdf_throughput_ul.png)

The same distribution for the uplink.

![CDF of Normalized UE Throughput, Uplink](mac/cdf_throughput_ul.png)

## UE Plots

### UE Trajectories
File: [ue/trajectory.png](ue/trajectory.png)

Shows the movement of all UEs in the simulated scenario, including their start and end points and the base station position.

![UE Trajectories](ue/trajectory.png)

### SINR Histogram
File: [ue/sinr_hist_ul_dl.png](ue/sinr_hist_ul_dl.png)

Shows the distribution of uplink and downlink SINR samples for each UE. It is useful for comparing radio quality and variability.

![SINR Histogram](ue/sinr_hist_ul_dl.png)

### RSRP Histogram
File: [ue/rsrp_hist_ul_dl.png](ue/rsrp_hist_ul_dl.png)

Shows the distribution of uplink and downlink RSRP samples for each UE. It gives a direct view of received power conditions.

![RSRP Histogram](ue/rsrp_hist_ul_dl.png)

### Uplink Throughput Histogram
File: [ue/rul_hist.png](ue/rul_hist.png)

Shows the distribution of uplink throughput samples for each UE.

![Uplink Throughput Histogram](ue/rul_hist.png)

### Downlink Throughput Histogram
File: [ue/rdl_hist.png](ue/rdl_hist.png)

Shows the distribution of downlink throughput samples for each UE.

![Downlink Throughput Histogram](ue/rdl_hist.png)

### Rank Indicator Histogram
File: [ue/ri_hist.png](ue/ri_hist.png)

Shows the distribution of the rank indicator values observed during the run.

![Rank Indicator Histogram](ue/ri_hist.png)

### Uplink Drop Sources
File: [ue/drop_sources_ul.png](ue/drop_sources_ul.png)

Shows all tracked uplink loss and congestion sources together, including radio errors and queue-related drops when present.

![Uplink Drop Sources](ue/drop_sources_ul.png)

### Downlink Drop Sources
File: [ue/drop_sources_dl.png](ue/drop_sources_dl.png)

Shows all tracked downlink loss and congestion sources together, including radio errors and queue-related drops when present.

![Downlink Drop Sources](ue/drop_sources_dl.png)

## Extended DualQ/L4S/NFQUEUE Plots

### Uplink CE Marks Histogram
File: [ue/ceul_hist.png](ue/ceul_hist.png)

Shows the distribution of congestion experienced marks observed in uplink.

![Uplink CE Marks Histogram](ue/ceul_hist.png)

### Downlink CE Marks Histogram
File: [ue/cedl_hist.png](ue/cedl_hist.png)

Shows the distribution of congestion experienced marks observed in downlink.

![Downlink CE Marks Histogram](ue/cedl_hist.png)

### Uplink AQM Drops Histogram
File: [ue/aqmdul_hist.png](ue/aqmdul_hist.png)

Shows the distribution of AQM drop events observed in uplink.

![Uplink AQM Drops Histogram](ue/aqmdul_hist.png)

### Downlink AQM Drops Histogram
File: [ue/aqmddl_hist.png](ue/aqmddl_hist.png)

Shows the distribution of AQM drop events observed in downlink.

![Downlink AQM Drops Histogram](ue/aqmddl_hist.png)

### CE Marks over Time
File: [ue/ce_marks_ul_dl.png](ue/ce_marks_ul_dl.png)

Shows the CE mark evolution over time for uplink and downlink.

![CE Marks over Time](ue/ce_marks_ul_dl.png)

### AQM Drops over Time
File: [ue/aqm_drops_ul_dl.png](ue/aqm_drops_ul_dl.png)

Shows AQM drop activity over time for uplink and downlink.

![AQM Drops over Time](ue/aqm_drops_ul_dl.png)

### L4S Queue Size over Time
File: [ue/l4s_queue_size_ul_dl.png](ue/l4s_queue_size_ul_dl.png)

Shows the L4S queue occupancy over time for uplink and downlink.

![L4S Queue Size over Time](ue/l4s_queue_size_ul_dl.png)

### Classic Queue Size over Time
File: [ue/classic_queue_size_ul_dl.png](ue/classic_queue_size_ul_dl.png)

Shows the Classic queue occupancy over time for uplink and downlink.

![Classic Queue Size over Time](ue/classic_queue_size_ul_dl.png)

### DualPI2 p_L over Time
File: [ue/dualpi2_pl_ul_dl.png](ue/dualpi2_pl_ul_dl.png)

Shows the DualPI2 low-latency marking probability over time.

![DualPI2 p_L over Time](ue/dualpi2_pl_ul_dl.png)

### DualPI2 p_C over Time
File: [ue/dualpi2_pc_ul_dl.png](ue/dualpi2_pc_ul_dl.png)

Shows the DualPI2 Classic marking or drop probability over time.

![DualPI2 p_C over Time](ue/dualpi2_pc_ul_dl.png)

### DualPI2 p_CL over Time
File: [ue/dualpi2_pcl_ul_dl.png](ue/dualpi2_pcl_ul_dl.png)

Shows the coupled DualPI2 probability term over time.

![DualPI2 p_CL over Time](ue/dualpi2_pcl_ul_dl.png)
