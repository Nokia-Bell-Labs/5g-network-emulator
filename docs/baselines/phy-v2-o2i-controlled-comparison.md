# Controlled n258 O2I Comparison

The paired profiles use identical map, traffic, mobility, power, gains, scheduler, duration, and keyed fast-fading/interference streams. The stress profile changes only the UE environment from outdoor/no penetration to indoor/high-loss building penetration.

| Direction | Outdoor throughput | High-loss throughput | Delta | Median-UE SINR delta | Outage UEs |
|---|---:|---:|---:|---:|---:|
| DL | 500.02 | 235.16 | -53.0% | -38.15 dB | 0 -> 0 |
| UL | 157.05 | 55.22 | -64.8% | -38.71 dB | 0 -> 0 |

Equivalent scalar FWA gains remain an alignment abstraction, not a beamforming model. The paired result isolates configured environment loss but is still one stochastic seed.
