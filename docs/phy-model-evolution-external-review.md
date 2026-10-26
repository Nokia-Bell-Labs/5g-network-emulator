# Physical and MAC-Layer Abstractions in FikoRE for 5G Application Emulation

## Abstract

FikoRE is a single-cell 5G radio-access-network emulator that exposes real or generated application traffic to controlled changes in coverage, capacity, latency, loss, mobility, and scheduling. It advances a packet-level model in 1 ms intervals and represents the relationships between position, propagation, received power, interference, link adaptation, radio-resource allocation, packet errors, retransmissions, and delivered payload. It does not generate waveforms or emulate a complete receiver.

The implemented model combines deterministic Alpha-Beta-Gamma path-loss and correlated-shadowing maps, explicit building and vehicle penetration states, resource-consistent signal and noise accounting, allocation-aware uplink power finalization, threshold-based MCS and rank selection, MT/PF/BET/RR scheduling, and a hardened legacy table-driven BLER/HARQ path. Integer packet accounting closes exactly across delivered, expired, queue-dropped, radio-dropped, and pending bits.

Validation covers 630 independently seeded maps, canonical packet-level scenarios, a 288-case scheduler matrix, deterministic and million-decision HARQ tests, and paired disabled/no-retry/production HARQ campaigns. The evidence establishes reproducibility and internal consistency, not deployment-level predictive accuracy. Interference remains a single-cell surrogate, rank and MIMO are scalar abstractions, and the legacy BLER table has no documented receiver or link-level calibration. The roadmap therefore focuses on calibrated multicell interference, beam and blockage state, carrier aggregation, MIMO, and link-to-system prediction beyond the implemented legacy HARQ path.

## 1. Purpose and modelling philosophy

FikoRE occupies a middle ground between a static link-budget calculator and a complete link- or system-level NR simulator. A link-budget calculator can estimate received power and theoretical capacity at one position, but it cannot expose an application to mobility, packet queues, TDD availability, scheduling competition, delay expiry, or changes in effective delivered payload. A full NR simulator can model many of those effects with substantially greater radio fidelity, but it also requires more scenario state, calibration data, runtime, and specialist knowledge. FikoRE instead models the radio mechanisms that are most visible to an application while retaining a 1 ms execution loop and a configuration surface that can be understood and modified without implementing a complete baseband chain.

The current model is intended to answer questions such as how offered traffic is divided among users with different radio conditions, how coverage and MCS affect nominal radio capacity, how uplink power limits interact with the number of allocated PRBs, how TDD and scheduler granularity affect service continuity, and how packet demand differs from nominal grid capacity. It is not intended to predict the exact BLER of a commercial receiver, perform site planning, reproduce beam-management procedures, or replace deployment-specific propagation calibration.

The original FikoRE architecture was introduced as an application-level RAN emulator rather than a standards-calibration simulator [21]. The physical-model work described here refines that architecture by making the bandwidth reference, uplink power semantics, map generation, penetration state, scheduler history, packet-error behavior, and validation boundary explicit. Comparable open simulators such as 5G-LENA and Simu5G provide broader NR system models and useful calibration precedents [19], [20], while the Vienna methodology illustrates the link-level calibration process that remains necessary before FikoRE can claim predictive MCS or BLER accuracy [18].

The main body is self-contained and describes model behavior rather than source-code layout. Reproduction commands, exact configuration files, source identities, campaign manifests, and artifact hashes are grouped in Appendix A and the evidence protocol.

## 2. End-to-end architecture

FikoRE advances the serving cell in transmission time intervals of \(\Delta t=1\ \mathrm{ms}\). The implementation uses a pipelined loop: scheduling at tick \(t\) consumes queue and channel state prepared by the UE phase of tick \(t-1\), then the UE phase at \(t\) advances packet release, mobility, and channel state for the next scheduler invocation. This one-step ordering is deterministic and should not be interpreted as an instantaneous channel update followed by scheduling inside the same tick.

| Execution stage at tick \(t\) | State transition |
|---|---|
| Control boundary | Apply commands while the MAC and UE workers are quiescent |
| MAC, packet, and HARQ | Rank UEs with previously prepared PHY/queue state, allocate resources, quantize grants, process transmissions and retries, and commit scheduler history |
| Queue maintenance | Advance packet time, ingest newly generated or captured traffic, release completed packets, and expire stale data |
| Mobility and channel preparation | Update position and, when due, derive map gain, penetration, fading, interference, SINR, rank, MCS, and nominal capacity for tick \(t+1\) |
| Telemetry | Publish packet, queue, mobility, and PHY state at their corresponding points in the pipeline |

The evaluated model contains one serving cell and one configured carrier. It supports TDD and symmetric FDD, although all reference evidence uses TDD. A carrier is represented by a rectangular time-frequency grid. Frequency resources may be scheduled as individual physical resource blocks or grouped into resource-block groups, while the time dimension may be localized over the complete 1 ms interval or distributed over the numerology-dependent slots inside that interval.

The physical layer supplies continuous nominal capacity per scheduling unit. The packet boundary quantizes this capacity to integer bits and may deliver fewer useful bits because a queue contains less data, a ready HARQ block does not fit the current grant, a transmission fails the configured BLER decision, or a packet expires under its delay budget. Active HARQ therefore introduces retransmission and radio-drop outcomes in addition to queue and expiry losses. These outcomes are operationally well-defined and exactly accounted, but they inherit the external-validity limits of the uncalibrated legacy BLER table.

### 2.1 Nomenclature

| Symbol or term | Meaning |
|---|---|
| UE | User equipment attached to the emulated serving cell |
| DL / UL | Downlink from gNB to UE / uplink from UE to gNB |
| TTI | One 1 ms MAC scheduling interval |
| PRB | Physical resource block containing 12 adjacent subcarriers |
| RBG | Configurable group of complete PRBs used as one scheduling unit |
| CQI | Channel-quality indicator derived from the selected spectral efficiency |
| MCS | Modulation-and-coding index selected from an SINR-threshold table |
| RI / rank | Number of simultaneously modelled spatial layers |
| O2I | Outdoor-to-indoor or environmental penetration state |
| MT / PF / BET / RR | Max Throughput / Proportional Fair / Blind Equal Throughput / Round Robin scheduler |
| HARQ | Retransmission process driven by an ACK/NACK-like statistical decision |
| `harq_model` | Configuration choice between active `legacy_bler` and the explicit `disabled` ablation |
| `throughput_intra_tti_update` | Configuration choice between no provisional update and allocation-unit PF/BET reranking |
| \(N_{\mathrm{RB,car}}\) | Number of modelled frequency-domain PRBs in the carrier |
| \(n_b\) | Number of PRBs in scheduling unit \(b\) |
| \(\Gamma_{i,b}\) | SINR of UE \(i\) on scheduling unit \(b\) |
| \(B_{i,b}\) | Nominal capacity bits granted to UE \(i\) on unit \(b\) |
| \(B^{\mathrm{eff}}_{i,b}\) | Effective packet payload delivered from that grant |
| \(\bar R_i\) | Exponentially weighted effective-service history for PF or BET UE \(i\) |

## 3. Time-frequency resource model

For numerology \(\mu\), FikoRE uses the NR subcarrier-spacing relation

\[
\Delta f = 15\cdot 2^\mu\ \mathrm{kHz}.
\]

Given a configured usable radio bandwidth \(B_{\mathrm{RF}}\), the frequency-domain PRB count is

\[
N_{\mathrm{RB,car}}=\left\lfloor\frac{B_{\mathrm{RF}}}{12\Delta f}\right\rfloor.
\]

A scheduling unit that groups \(n_b\) PRBs contains \(N_{\mathrm{SC},b}=12n_b\) subcarriers. Grouped scheduling uses only complete RBGs, so a carrier may contain a small remainder of modelled PRBs that is not represented by a scheduling unit. This is a resource-granularity limitation rather than an SINR-bandwidth ambiguity: total downlink carrier power is divided by the physical carrier PRB count, not by the number of scheduled groups.

Localized scheduling uses one time group over the complete TTI. Distributed scheduling divides the TTI into \(2^\mu\) time groups. The number of independent scheduler decisions per direction and TTI is therefore

\[
N_{\mathrm{dec}}=N_{\mathrm{freq\ units}}N_{\mathrm{time\ units}}.
\]

This decision count is a primary runtime parameter. A grouped 100 MHz grid may require only 17 decisions per direction and TTI, whereas a distributed per-PRB 400 MHz grid requires approximately 2,000.

For TDD, a configured symbol pattern determines which symbols are physically available to each direction. DL-ineligible UL symbols, UL-ineligible DL symbols, and transition symbols are structural duplexing resources and must not be counted as scheduler failures. FikoRE reports structural unavailability separately from available-but-empty, assigned, and effective-payload units. Directional assignment fill is

\[
U_d=\frac{N_{\mathrm{assigned},d}}{N_{\mathrm{available},d}},
\]

and effective fill uses the same denominator but counts only assigned units that produce positive effective payload.

The current implementation supports symmetric FDD bandwidth division. Asymmetric FDD is rejected because DL and UL presently share one carrier-grid descriptor; supporting asymmetric division safely would require direction-specific PRB, RBG, coherence, and HARQ state.

## 4. Current physical-layer model

### 4.1 Scenario geometry and spatial maps

The serving gNB is placed at the origin of a two-dimensional horizontal coordinate system. Each UE has a configured or randomly generated position and a mobility model. The physical model samples one deterministic spatial map for the selected scenario and carrier frequency. The production catalog contains Rural Macrocell, Urban Macrocell, Urban Microcell, indoor open office, indoor mixed office, and indoor shopping-mall maps at the frequencies required by the reference scenarios and associated studies.

Each current map contains 291 by 291 samples with one explicit center cell. If \(c\) is the map spacing and \(N=291\), runtime coordinates are transformed to map indexes as

\[
i_x=\frac{x}{c}+\frac{N-1}{2},\qquad i_y=\frac{y}{c}+\frac{N-1}{2}.
\]

The runtime obtains off-grid gain by bilinear interpolation. The generated nodes have a binary LOS or NLOS state, but interpolation between unlike nodes blends their already-selected dB gains; the runtime map should therefore be interpreted as a continuous effective large-scale-gain field rather than a categorical LOS decision at every possible coordinate.

Map dimensions are chosen to provide a reusable finite field, not to assert that a propagation fit is valid over the entire square. Current corner distances are approximately 1.03 km for UMi and shopping-mall maps, 0.62 km for office maps, and 3.79 km for UMa and RMa maps. Mobility emits a warning when the requested radius exceeds the map corner.

### 4.2 ABG path loss

The map stores negative path loss plus shadowing, expressed as link gain in dB. For propagation state \(q\in\{\mathrm{LOS},\mathrm{NLOS}\}\), the retained Alpha-Beta-Gamma model is

\[
PL_q(d,f)=10\alpha_q\log_{10}\!\left(\frac{\max(d,1\ \mathrm{m})}{1\ \mathrm{m}}\right)+\beta_q+10\gamma_q\log_{10}\!\left(\frac{f}{1\ \mathrm{GHz}}\right).
\]

The corresponding state-specific gain field is

\[
G_q(\mathbf{x})=S_q(\mathbf{x})-PL_q(d(\mathbf{x}),f),
\]

where \(S_q\) is correlated lognormal shadowing represented as a zero-mean Gaussian field in dB. The ABG family was selected in the earlier FikoRE physical-layer redesign because it provides one computationally simple structure across outdoor, indoor, sub-6-GHz, and millimetre-wave profiles [6]–[8]. The retained coefficients originate from measurement-derived fits, but their exact campaign ranges and extrapolation limits still require a consolidated provenance table before deployment-level predictions can be claimed.

### 4.3 LOS state and correlated shadowing

The target shadow covariance follows the exponential Gudmundson form used by the retained two-dimensional shadowing method [9]:

\[
\rho_q(\Delta r)=\exp\!\left(-\frac{|\Delta r|}{d_{\mathrm{cor},q}}\right),
\]

with a scenario- and state-specific decorrelation distance. A zero-mean Gaussian field is generated from this covariance by padded FFT embedding followed by center cropping. Padding prevents the opposite production-map edges from becoming artificial periodic neighbours.

The LOS state is generated from a separate correlated Gaussian field \(Z(\mathbf{x})\). Transforming that field with the standard normal cumulative distribution gives \(U(\mathbf{x})=\Phi(Z(\mathbf{x}))\), and the binary state is

\[
L(\mathbf{x})=\mathbb{1}\!\left[U(\mathbf{x})\le p_{\mathrm{LOS}}(d(\mathbf{x}),h_{\mathrm{map}})\right].
\]

The final node gain is

\[
G_{\mathrm{map}}(\mathbf{x})=L(\mathbf{x})G_{\mathrm{LOS}}(\mathbf{x})+\left(1-L(\mathbf{x})\right)G_{\mathrm{NLOS}}(\mathbf{x}).
\]

The RMa, UMi, UMa, and indoor-office LOS probabilities follow the retained scenario formulas from TR 38.901 [1]. The UMa height term is evaluated when the map is generated, using \(h_{\mathrm{map}}=1.5\ \mathrm{m}\) for the production catalog; runtime UE height does not regenerate the LOS state. The shopping-mall LOS probability is a legacy FikoRE heuristic and is explicitly not attributed to a fixed TR 38.901 shopping-mall equation.

### 4.4 Environmental and penetration loss

UE environment is represented independently from the macroscopic map. A UE can be configured as outdoor, indoor, inside a vehicle, or randomly assigned according to a scenario-specific probability. Explicit states are preserved in every scenario; only the random state invokes scenario probabilities. The building-penetration profile can be disabled or set to low-loss or high-loss, and the vehicle profile can use standard or metallized glazing.

For an indoor UE with an enabled facade profile, additional loss is

\[
L_{\mathrm{add}}=L_{\mathrm{wall}}+0.5d_{\mathrm{in}}+a_{\mathrm{O_2}}(f)\frac{d}{1000}\quad[\mathrm{dB}],
\]

where \(d_{\mathrm{in}}\) is indoor depth in metres and \(a_{\mathrm{O_2}}(f)\) is the frequency-dependent oxygen attenuation in dB/km. Oxygen attenuation is applied independently of environment type. An indoor scenario in which gNB and UE are already in the same building uses no exterior-facade loss.

Wall loss uses a material-mixture model

\[
L_{\mathrm{wall}}=\max\!\left[0,\ 5-10\log_{10}\!\left(\sum_m p_m10^{-L_m(f)/10}\right)+X_\sigma\right].
\]

For low-loss facades, the material weights are 0.3 standard glass and 0.7 concrete with \(\sigma=4.4\ \mathrm{dB}\). For high-loss facades, the weights are 0.7 IRR glass and 0.3 concrete with \(\sigma=6.5\ \mathrm{dB}\). Material losses are

\[
L_{\mathrm{glass}}=2+0.2f_{\mathrm{GHz}},\qquad L_{\mathrm{IRR}}=23+0.3f_{\mathrm{GHz}},\qquad L_{\mathrm{concrete}}=5+4f_{\mathrm{GHz}}\quad[\mathrm{dB}].
\]

A UE-shared environment stream supplies one building-loss draw, one vehicle-loss draw, and a deterministic sequence of indoor-depth candidates. For UMi and UMa, candidate depth is based on \(D_{\max}=25\ \mathrm{m}\); for RMa it uses \(D_{\max}=10\ \mathrm{m}\):

\[
d_j=\min(U_{j,1},U_{j,2})D_{\max}.
\]

At first channel evaluation, the first candidate satisfying \(d_j<d\) is selected, or the final candidate is clipped to the link distance. DL and UL use the same environmental realization, and separate keyed random streams prevent penetration configuration from shifting fast-fading or interference sequences.

Vehicle loss is

\[
L_{\mathrm{vehicle}}=\max(0,\mu_v+5Z)\quad[\mathrm{dB}],
\]

where \(\mu_v=9\ \mathrm{dB}\) for standard glazing and \(\mu_v=20\ \mathrm{dB}\) for metallized glazing. Vehicle UEs do not receive the building indoor-depth term.

### 4.5 Small-scale fading and spatial update

When stochastic fading is enabled, the resource-level power gain is

\[
F_{i,b}=10\log_{10}\!\left(\frac{X^2+Y^2}{2}\right),\qquad X,Y\sim\mathcal N(0,1),
\]

which has unit mean in linear power. Independent keyed streams are used for DL and UL. The present implementation applies Rayleigh fading to both LOS and NLOS links and holds or redraws the coefficient according to a coherence approximation expressed on scheduling units. Consequently, changing resource grouping can change the number and placement of stochastic fading samples even though deterministic carrier power and noise remain grouping-invariant. A future calibrated LOS model may replace universal Rayleigh fading with scenario-dependent Rician or beam-conditioned statistics.

The macroscopic map is spatially consistent by construction. Runtime samples the map at the current UE position whenever the SINR refresh cadence expires. The implementation also tracks movement relative to a scenario correlation distance and updates its reference position after that distance is exceeded, but this movement state does not currently gate map sampling; it is retained as preparation for a future spatial-consistency update policy.

### 4.6 Downlink power

Configured downlink power is interpreted as total power over the modelled carrier, rather than as EIRP or per-PRB power [2]. It is distributed uniformly over the physical frequency-domain PRBs:

\[
P_{\mathrm{DL,PRB}}=P_{\mathrm{DL,tot}}-10\log_{10}N_{\mathrm{RB,car}}\quad[\mathrm{dBm}].
\]

This denominator is independent of RBG size. Grouping can still change the final represented capacity because only complete RBGs are scheduled and because the current stochastic fading and MCS-threshold resolution follow scheduling units.

### 4.7 Uplink power control

FikoRE supports fixed total UE power and a simplified fractional power-control mode. In fixed mode, the configured value is the UE total transmit-power budget. In fractional mode, the nominal per-PRB value is

\[
P_{\mathrm{nom,PRB}}=P_0+\alpha PL_{\mathrm{pc}}\!\left(\max[d-d_{\mathrm{in}},1\ \mathrm{m}]\right),
\]

where \(P_0\) is a configured nominal target, \(\alpha\) is the fractional path-loss compensation factor, and \(PL_{\mathrm{pc}}\) is a LOS ABG control-path estimate rather than the complete map and penetration realization. If UE \(i\) receives \(M_i\) PRBs, final power is

\[
\begin{aligned}
P_{i,\mathrm{tot}}&=\min\!\left(P_{\mathrm{nom,PRB}}+10\log_{10}M_i,\ P_{\mathrm{UE}}\right),\\
P_{i,\mathrm{PRB}}&=P_{i,\mathrm{tot}}-10\log_{10}M_i.
\end{aligned}
\]

\(P_{\mathrm{UE}}\) is the configured `tx_power_ul`. Fixed mode transmits that total. Fractional mode uses it only as a ceiling: a request below it is left unchanged, and there is no floor that raises total power. This is a reduced form of NR PUSCH power control [3]; it omits closed-loop commands, transport-format compensation, several numerology-dependent reference details, and simultaneous-carrier power sharing.

Uplink scheduling is two-stage. Candidate grants are first evaluated with the most recently finalized per-PRB power. After all assignments in one time group are known, the UE total power is redistributed over the current PRBs and SINR, MCS, and nominal bits are recomputed. The scheduler does not perform a second allocation pass after that correction, so the mechanism prevents repeated use of the complete UE power budget but remains an order-dependent heuristic rather than a joint power-and-scheduling optimum.

Delivered scenarios use fractional mode. Study UEs use 23 dBm. UMa, RMa, and n40 background UEs also use 23 dBm, whereas indoor and n258 background UEs use 10 dBm. In fractional mode that value is only a ceiling. Fractional power control has algebraic and UE-level finalization tests. A paired packet-level campaign against fixed mode is recorded in `docs/ul-fractional-power-campaign.md`. Historical baseline inputs record `ul_power_mode: fixed`, the new name of the former `set_ul_pow: true` setting, so rerunning them stays in fixed mode.

### 4.8 Thermal noise and interference

Thermal noise is integrated over the same per-PRB bandwidth used by desired signal and interference:

\[
P_{N,\mathrm{PRB}}=N_0+NF+10\log_{10}(12\Delta f)\quad[\mathrm{dBm}],
\]

where \(N_0=-174\ \mathrm{dBm/Hz}\) in the reference configurations and \(NF\) is receiver noise figure. Noise and interference are combined in linear power:

\[
P_{N+I,\mathrm{PRB,dBm}}=10\log_{10}\!\left(1000\left[P_{N,\mathrm{PRB,W}}+P_{I,\mathrm{PRB,W}}\right]\right).
\]

The current interference model is an aggregate per-PRB surrogate. It uses a configured interferer count, overlap ratio, random power, distance offset, and LOS ABG attenuation. It has no neighbouring-cell geometry, load state, antenna pattern, beam state, or neighbour scheduler. The UL surrogate samples a per-PRB value over \([-23,+23]\ \mathrm{dBm}\) and omits interferer penetration and antenna gain; it must not be interpreted as a calibrated total-power UE model or as deployment-predictive received interference.

### 4.9 Resource-level SINR

Let \(G_{\mathrm{map}}(\mathbf{x}_i)\) be the interpolated map gain of UE \(i\), \(L_{\mathrm{add},i}\) its environmental loss, \(G_{\mathrm{tx}}\) and \(G_{\mathrm{rx}}\) scalar antenna gains, \(F_{i,b}\) small-scale power gain, \(v_i\) rank, and \(\Delta_i\) an optional configured offset. Resource-level SINR is

\[
\Gamma_{i,b}=P_{i,\mathrm{PRB}}+G_{\mathrm{tx}}+G_{\mathrm{rx}}+G_{\mathrm{map}}(\mathbf{x}_i)-L_{\mathrm{add},i}+F_{i,b}-10\log_{10}v_i-P_{N+I,\mathrm{PRB,dBm}}+\Delta_i.
\]

All additive terms are expressed in dB or dBm according to their role. The equal-power rank penalty is a scalar abstraction; it is not a post-processing SINR obtained from a channel matrix.

### 4.10 MCS, rank, and nominal capacity

With table-based adaptation enabled, MCS is selected as

\[
m_{i,b}=\max\left\{m:\Gamma_{i,b}\ge\theta_{m,n_b,\ell_{\mathrm{cfg}}}\right\},
\]

where \(\theta\) depends on MCS, allocation-size class, and configured layer count. If SINR is below the first threshold, MCS is set to \(-1\), spectral efficiency is zero, and the UE is not a positive-rate candidate on that unit. This threshold is an internal table boundary, not a separately configured minimum-SINR admission rule; NR MCS procedures provide the standards context but not calibration for these retained thresholds [4].

For spectral efficiency \(\eta_m\), usable symbols \(N_{\mathrm{sym},b}\), subcarriers \(N_{\mathrm{SC},b}\), rank \(v_i\), direction- and frequency-dependent overhead \(o\), and scaling factor \(s_i\), nominal capacity is

\[
B_{i,b}=v_iN_{\mathrm{SC},b}N_{\mathrm{sym},b}\eta_m(1-o)s_i.
\]

This quantity is a continuous capacity proxy, not an NR transport-block calculation. It does not perform TBS rounding, CRC and code-block segmentation, rate matching, DMRS allocation, or codeword mapping. MCS thresholds likewise have no committed link-level receiver and BLER calibration dataset.

DL rank is selected from thresholds applied to mean SINR and is bounded by configured UE antennas and available layers; UL rank is one. The table axis is safely converted from one-based layer count to zero-based storage. Rank above one remains weakly modelled: selection observes SINR after the previous rank's equal-power penalty, no hysteresis is applied, and MCS thresholds remain tied to configured layers rather than selected dynamic rank. The evaluated reference profiles are rank one.

## 5. Interaction with packet handling and MAC scheduling

### 5.1 Nominal grants and effective payload

The scheduler assigns continuous nominal capacity \(B_{i,b}\), while the packet layer reports integer effective bits \(B^{\mathrm{eff}}_{i,b}\). Capacity is quantized once at the PDCP boundary and only a sub-bit residual is carried forward. Effective payload can be lower because the queue contains fewer bits, a ready retry does not fit the grant, an initial transmission or retry fails, or a packet expires. Resource summaries therefore distinguish available units, assigned units, units with positive effective payload, and assigned units that produce no effective payload.

A retransmission is attempted only when the current grant can carry the complete stored HARQ block. A smaller grant may carry fresh data on another logical HARQ process while the larger retry waits; if no fresh data exists, that allocation produces no air charge or effective payload. Charged bits equal the transmitted block, effective payload cannot exceed charged bits, and rate-cap tokens use charged rather than candidate capacity. The retry retains its original MCS and selected rank while the current SINR drives the table lookup. Packet, fragment, queue, fate, and cumulative counters are integer bits and satisfy an exact admitted-equals-terminal-plus-pending invariant.

### 5.2 Throughput scheduler family

MT, PF, and BET are configuration-distinct schedulers backed by one internal metric:

\[
M_{i,b}(t)=w_i\frac{r_{i,b}(t)^\alpha}{\max(\bar R_i(t),\epsilon)^\beta}.
\]

Here \(w_i\) is priority, \(r_{i,b}\) is current nominal achievable rate, and \(\bar R_i\) is effective-service history. MT uses \((\alpha,\beta)=(1,0)\) and never enters the history lifecycle. PF uses \((\alpha,\beta)=(\alpha_{\mathrm{PF}},1)\), where \(\alpha_{\mathrm{PF}}\) is cell-wide. Pure BET uses \((0,1)\) and therefore ranks inverse history independently of instantaneous rate once eligibility is established. RR retains an independent cursor implementation, ignores priority, and never enters this formula. The PF rate-over-history recipe follows established proportional-fair scheduling principles [10], [11].

| Configured alias | Scheduler behavior |
|---|---|
| `metric_type: 4` | MT: current weighted rate, no history |
| `metric_type: 6` | PF: weighted \(r^{\alpha_{\mathrm{PF}}}/\bar R\) |
| `metric_type: 1` | BET: inverse service history after eligibility |
| `metric_type: 5` | RR: independent rotating cursor |

PF and BET history is updated once per active 1 ms TTI:

\[
\begin{aligned}
a&=1-\exp(-1/\tau_{\mathrm{ms}}),\\
\bar R_i(t+1)&=(1-a)\bar R_i(t)+a\,x_i(t),
\end{aligned}
\]

where \(x_i(t)\) is effective payload served during the TTI. An active positive-rate UE that receives no effective payload contributes zero. An idle UE freezes its history, detach resets it, and a newly active UE starts from its standalone achievable rate. The configured emulator CQI-refresh cadence updates the instantaneous achievable-rate numerator; it is not an RRC reporting model and does not control service-history updates.

Exact metric comparisons use an absolute tolerance of \(10^{-6}\). Candidates within that tolerance share a persistent round-robin tie cursor so container order does not define long-term service.

### 5.3 Intra-TTI reranking

Without intra-TTI reranking, all allocation units in a TTI see the same committed PF or BET denominator, so one user can win several units before history changes. Setting `throughput_intra_tti_update: allocation_unit` makes the scheduler maintain a provisional current-TTI service value

\[
\tilde R_i(t,b)=(1-a)\bar R_i(t)+a\,x_i^{\mathrm{nom}}(t,b),
\]

where \(x_i^{\mathrm{nom}}(t,b)\) is cumulative nominal service already planned in the current TTI. The next unit is reranked with this projected denominator. At TTI end, only one committed update is made, using effective payload rather than provisional nominal bits.

For UL, planning and provisional reranking occur before allocation-aware power is finalized. Final power and rate corrections do not trigger a second scheduling pass. This preserves bounded runtime but is an explicit approximation.

### 5.4 Legacy BLER and HARQ

The production default `harq_model: legacy_bler` restores the historical static table with shape \(2\times5\times4\times28\times60\): modulation table, RBG class \(1/2/4/8/16\) PRBs, one through four layers, MCS 0 through 27, and bounded SINR bin. Every axis and finite SINR are checked before lookup; unsupported MCS 28 is rejected. The 67,200 packed float64 values have SHA-256 `d61acbe2a5cea399570c53b40f0374261ca28ac80ccefed0aee85728ea9bda70`, contain 544 duplicate curves, and contain no adjacent BLER increase with SINR.

For table BLER \(p\) and zero-based attempt ordinal \(n\), FikoRE preserves the historical failure law

\[
P_{\mathrm{fail}}(p,n)=\operatorname{clip}_{[0,1]}\left[\left(1-(1-p)^{n+1}\right)0.8^{n-1}\right].
\]

Attempt zero is the initial transmission and attempts \(1,\ldots,N_{\max}\) are retries. This expression is not claimed to represent Chase combining or incremental redundancy and is non-monotonic for some early attempts; it is retained for reproducibility pending external calibration. `max_rtx` is authoritative, each queued block has its own ready deadline, and BLER, feedback-period, processing-delay, and propagation-jitter streams are independent. `disabled` remains an explicit ablation mode.

Captured real traffic uses one completion state per original packet. Successful and failed fragments share one ordering structure, and the packet receives exactly one final accept, congestion-marked accept, or drop decision. If the operating-system verdict send fails, FikoRE retains the state and retries; detach and shutdown stop reception before draining all copied packets. This operational hardening removes a prior capture-deadlock class but does not improve the physical validity of the BLER values.

## 6. Reference physical configurations

The following profiles define representative, internally consistent single-carrier configurations used for validation. They are not universal deployment defaults.

| Profile | Scenario and carrier | DL carrier power | Scalar gNB/UE gain | NF gNB/UE | UL power |
|---|---|---:|---:|---:|---|
| UMi n40 NPN | UMi, 2.38 GHz, 20 MHz, \(\mu=1\) | 43 dBm | 8.7/0 dBi | 2/9 dB | 23 dBm |
| UMa n78 pedestrian | UMa, 3.5 GHz, 100 MHz, \(\mu=1\) | 46 dBm | 8.7/0 dBi | 2/9 dB | 23 dBm |
| RMa n78 vehicular | RMa, 3.5 GHz, 100 MHz, \(\mu=1\) | 46 dBm | 8.7/0 dBi | 2/9 dB | 23 dBm |
| Indoor n78 pedestrian | Indoor open office, 3.5 GHz, 100 MHz, \(\mu=1\) | 24 dBm | 8.7/0 dBi | 2/9 dB | 23 dBm study UE; 10 dBm background |
| UMi n258 FWA | UMi, 26 GHz, 400 MHz, \(\mu=3\) | 35 dBm | 24/26 dBi | 7/10 dB | 23 dBm study UE; 10 dBm background |

The same profiles also define a repeatable MAC policy rather than leaving scheduler behavior implicit:

| Profile | Scheduler | Intra-TTI policy | HARQ policy | Study queue |
|---|---|---|---|---|
| UMi n40 NPN | PF | Allocation-unit reranking | `legacy_bler`, four retries | Legacy single queue |
| UMa n78 pedestrian | PF | Allocation-unit reranking | `legacy_bler`, four retries | Legacy single queue |
| RMa n78 vehicular | RR | Not applicable | `legacy_bler`, four retries | Legacy single queue |
| Indoor n78 pedestrian | PF | Allocation-unit reranking | `legacy_bler`, four retries | Legacy single queue |
| UMi n258 FWA | PF | Allocation-unit reranking | `legacy_bler`, four retries | Legacy single queue |

All five general-purpose study UEs disable the optional DualQ/L4S queue by default. Dedicated live-traffic comparison profiles enable L4S explicitly when the experiment is intended to compare scalable and legacy congestion control.

Configured DL power is treated as total carrier power. The distinction between conducted power, total radiated power, and EIRP must remain explicit when mapping these profiles to equipment. The n258 gains are equivalent aligned FWA gains and do not constitute a beamforming model.

## 7. Validation methodology

### 7.1 Deterministic and unit-level validation

Deterministic validation is grouped by model boundary so that a failure can be assigned to one contract:

| Boundary | Main checks |
|---|---|
| Carrier and power | Per-PRB signal/noise reference, grouped/per-PRB SINR invariance, FR1/FR2 overhead, and UL power conservation over 1–275 PRBs |
| Link adaptation and MIMO indexes | MCS boundaries, valid one-to-four-layer table indexes, bounded rank, and explicit rejection of unsupported axes |
| Scheduler and duplexing | MT/PF/BET formulas, RR independence, one history update per active TTI, tie rotation, TDD structural-resource accounting, and unsupported asymmetric FDD rejection |
| Packet and HARQ | Integer-bit closure, charged-grant conservation, retry limits 0/1/4, per-block deadlines, independent RNG streams, bounded queues, capture verdict retry, and clean shutdown |
| Environment and maps | Shared DL/UL penetration realization, independent keyed streams, explicit map origin, exact-frequency selection, and deterministic catalog hashes |

Stress validation adds one million deterministic HARQ decisions, 100,000 queue cycles, saturated-queue accounting, sanitizer runs, and recursive verification of every committed artifact and the embedded BLER-table payload.

### 7.2 Map ensemble

Thirty independent master seeds, `20270000` through `20270029`, are evaluated for each of the 21 scenario-frequency entries, giving 630 maps. The realization, not the spatial cell, is the independent statistical unit. Diagnostics include radial LOS probability over the inscribed disk, link-gain quantiles, shadow mean and standard deviation, axial correlation at the nearest representable target lag, and opposite-edge correlation to detect periodic seams. Confidence intervals are pointwise and describe generator variability; they are not simultaneous goodness-of-fit tests or field-prediction intervals.

### 7.3 Packet-level profiles

Five principal profiles are run for 180 simulated seconds with seed `20260927` and a 20-second analysis warm-up, with n258 high-loss added as a sixth HARQ stress profile. The profiles contain 11 UEs for UMi n40, RMa, indoor n78, and n258, and 21 UEs for UMa n78. Reported quantities include offered and delivered throughput, queue, expiry, and radio errors, SINR and MCS quantiles, sampled outage classification, non-overlapping zero-delivery windows, observed delivery gaps inside contiguous positive-offer segments, resource assignment, effective-unit fill, payload-to-grant efficiency, retransmitted bits, HARQ occupancy and age, and exact closure residual.

The packet table in Section 8.2 comes from the explicit `harq_model: disabled` arm and is retained as the queue/scheduler ablation baseline. The shipped profiles use active `legacy_bler`; their paired disabled, no-retry, and four-retry results are reported separately in Section 8.5. This separation prevents a disabled-HARQ table from being mistaken for production behavior.

A UE is classified as sampled radio outage when its MCS is below zero in at least 99% of post-warm-up radio samples. Queue and HARQ occupancy are logged, but delivery gaps still combine offered traffic, positive-rate eligibility, scheduling, retries, expiry, and release timing. A delivery gap is therefore not labelled scheduler starvation unless those causes are conditioned separately.

### 7.4 Controlled map and penetration comparisons

The legacy-map and current-map batches use identical code, traffic, mobility, seed, duration, and analysis; only the selected map catalog differs. The historical n40 batch uses the previous nearest 3.5 GHz UMi map, while the current catalog includes an exact 2.38 GHz map.

The penetration comparison uses the nominal and high-loss n258 profiles from the disabled arm of the final common-seed campaign. The two configurations use the same map, traffic, mobility, powers, gains, scheduler, duration, and keyed fast-fading and interference streams; only the explicit outdoor/no-wall versus indoor/high-loss environment changes. The experiment is a one-seed controlled ablation rather than a population estimate. An older standalone O2I artifact is retained for history but is not the numerical source used below.

### 7.5 Runtime and scheduler functional evaluation

Three complementary experiments answer different scheduler questions. The scheduler-family matrix covers MT, BET, PF, and RR over 288 combinations of homogeneous or heterogeneous channels, finite or full-buffer demand, 16 or 64 UEs, three grids, and grouped or distributed/per-PRB allocation. Each case uses 20 warm-up TTIs followed by 100 measured TTIs and three repetitions, giving 300 timings per case and 86,400 timings overall.

A separate runtime-envelope experiment extends the population to 1, 16, 64, and 256 UEs and uses ten repetitions, giving 1,000 TTI timings per case. A third, longer PF experiment uses 500 warm-up TTIs and 2,000 measured TTIs to examine convergence relative to the 100 ms history window. The 1 ms threshold used in timing tables is a host-specific compute budget, not an end-to-end real-time guarantee.

### 7.6 HARQ campaign

The six profiles are paired under one geometry, traffic, mobility, and seed in three modes: explicit `disabled`, active legacy BLER with `max_rtx=0`, and active legacy BLER with the production retry limit four. The production configuration is then repeated under two additional seeds for retry-statistic sensitivity. This design separates the first-attempt table decision from retransmission policy and from pre-existing queue or expiry loss.

## 8. Validation results

### 8.1 Spatial-map diagnostics

Two independent generations of the complete 21-map catalog are byte-identical. Across the 30-realization ensembles, the largest absolute bias of ensemble-mean radial LOS probability is 0.038, the largest absolute error of ensemble-mean axial shadow correlation is 0.007, and the largest absolute ensemble-mean opposite-edge correlation is 0.026. Per-realization shadow standard deviation matches the configured value because each field is normalized by construction.

The map-only full-channel 0 dB SNR proxy is 100.0% for UMi 2.38 GHz, 98.5% for gain-assisted UMi 26 GHz, 78.5% for RMa 3.5 GHz, 38.2% for UMa 3.5 GHz, and 26.4% for indoor open office at 3.5 GHz. These fractions cover the complete stored map, including distances outside typical indoor layouts and potentially outside the source measurement ranges; they are diagnostics, not coverage predictions.

In the same-code fixed-seed legacy-versus-current catalog comparison, current maps change DL throughput by \(-2.0\%\) for indoor n78, \(+14.1\%\) for RMa n78, \(-8.5\%\) for UMa n78, approximately \(0\%\) for demand-saturated n258, and \(-2.5\%\) for n40. That experiment predates activation of the legacy BLER decision and changes only map bytes under its then-current packet model; its values should not be combined numerically with the production-HARQ table in Section 8.5. The deltas are one-realization observations rather than expected field-performance changes.

### 8.2 Packet-level reference profiles

The following table is the pre-HARQ disabled ablation and remains useful as the queue, expiry, and scheduler baseline for the paired campaign.

| Profile | Dir. | Offered (Mbit/s) | Delivered (Mbit/s) | Sampled outage UEs | Zero-delivery 1 s | Maximum observed UE delivery gap | Assigned/effective units | Payload/grant |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| Indoor n78 | DL | 65.00 | 42.54 | 0 | 0.0% | 0.81 s | 86.7% / 86.7% | 21.4% |
| Indoor n78 | UL | 90.00 | 60.36 | 0 | 0.0% | 0.09 s | 100.0% / 60.8% | 46.5% |
| RMa n78 | DL | 120.01 | 52.09 | 0 | 18.6% | 102.09 s | 96.1% / 96.1% | 99.1% |
| RMa n78 | UL | 120.01 | 26.95 | 0 | 3.2% | 5.10 s | 99.9% / 57.7% | 86.7% |
| UMa n78 | DL | 180.01 | 103.80 | 2 | 10.4% | 76.76 s | 99.0% / 99.0% | 73.5% |
| UMa n78 | UL | 70.00 | 13.37 | 0 | 28.2% | 160.00 s | 100.0% / 17.8% | 100.0% |
| UMi n258 | DL | 500.02 | 500.02 | 0 | 0.0% | 0.00 s | 99.5% / 99.5% | 39.5% |
| UMi n258 | UL | 200.01 | 157.05 | 0 | 0.0% | 0.02 s | 100.0% / 28.8% | 22.1% |
| UMi n40 | DL | 50.00 | 46.55 | 0 | 0.0% | 0.07 s | 100.0% / 100.0% | 71.9% |
| UMi n40 | UL | 35.00 | 24.30 | 0 | 0.0% | 0.03 s | 100.0% / 83.8% | 76.5% |

The n40 profile illustrates why a zero grant in one TTI must not automatically be labelled starvation. It contains many 10 ms windows with no delivered payload, but no 1 s zero-delivery windows and maximum observed gaps of 70 ms DL and 30 ms UL in this run.

Longer application-delivery gaps occur in UMa and RMa. UMa combines a high assignment ratio with individual non-outage delivery gaps up to the observation boundary, while RMa DL shows long periods without delivered payload despite assigning most available resources. The original baseline logs did not identify one cause; the active-HARQ evidence adds queue, retry, radio-drop, and conservation telemetry but still does not turn delivery gaps into a pure scheduler-starvation measure.

Resource assignment and useful occupancy must also be distinguished. RMa UL assigns 99.9% of available units but only 57.7% produce positive effective payload; UMa UL assigns 100.0% while only 17.8% produce effective payload. TDD-unavailable symbols are excluded from both denominators.

### 8.3 Controlled penetration result

For the n258 FWA pair, the difference between the two arms' median UE P50 SINRs is 38.15 dB DL and 38.71 dB UL. This is a difference of arm-level medians; the median of the paired per-UE P50 reductions is 36.48 dB DL and 36.81 dB UL. Delivered throughput falls from 500.02 to 235.16 Mbit/s in DL and from 157.05 to 55.31 Mbit/s in UL, corresponding to reductions of 53.0% and 64.8%. For every UE, the standard deviation across its paired DL P05, P50, and P95 SINR reductions is below \(5.9\times10^{-6}\) dB, showing that the quantile shifts remain aligned under the keyed random streams. The result isolates configured environment loss for one seed; it does not validate the scalar FWA gains as a beamforming model.

### 8.4 Scheduler reranking and execution cost

#### Short scheduler-family matrix

The generalized matrix contains 288 cases and 86,400 warmed-up TTI timings across all aliases, two UE populations, three grids, grouped and distributed/per-PRB resolution, finite and full-buffer demand, and homogeneous and heterogeneous channels. A representative 20 MHz, 16-UE, localized/grouped, full-buffer result shows the intended trade-off:

| Scheduler | Homogeneous throughput / Jain | Heterogeneous throughput / Jain | Heterogeneous maximum gap |
|---|---:|---:|---:|
| BET, allocation-unit | 66.12 Mbit/s / 0.982 | 51.29 Mbit/s / 0.303 | 100 TTIs |
| Max Throughput | 66.12 Mbit/s / 0.982 | 66.47 Mbit/s / 0.487 | 100 TTIs |
| Round Robin | 66.21 Mbit/s / 1.000 | 61.76 Mbit/s / 0.987 | 3 TTIs |
| PF, allocation-unit | 66.30 Mbit/s / 1.000 | 61.78 Mbit/s / 0.989 | 5 TTIs |

The heterogeneous arm keeps both groups eligible by pairing 50 m outdoor UEs with 200 m low-loss-indoor UEs. The 100-TTI measured window follows 20 warm-up TTIs and is deliberately short. Pure BET's inverse-history score strongly favors the initially lower-rate group and has not converged within this window, while MT maximizes aggregate rate by starving that group. RR and PF retain high short-window fairness with a modest aggregate-rate cost. These are scheduler characterizations, not universal asymptotic rankings.

#### Long PF functional run

The longer PF campaign uses 64 homogeneous UEs, 500 warm-up TTIs, and 2,000 measured TTIs. Jain fairness approaches one with and without reranking, while allocation-unit reranking substantially reduces maximum effective-service gaps:

| Grid | Scheduling unit | DL Jain none → rerank | Maximum DL effective-service gap none → rerank |
|---|---|---:|---:|
| 20 MHz, \(\mu=1\) | grouped | 0.9990 → 0.9999 | 96 → 18 TTIs |
| 100 MHz, \(\mu=1\) | grouped | 0.9990 → 1.0000 | 96 → 6 TTIs |
| 400 MHz, \(\mu=3\) | grouped | 0.9994 → 1.0000 | 81 → 6 TTIs |
| 20 MHz, \(\mu=1\) | per-PRB | 0.9990 → 1.0000 | 96 → 2 TTIs |
| 100 MHz, \(\mu=1\) | per-PRB | 0.9990 → 1.0000 | 96 → 1 TTI |
| 400 MHz, \(\mu=3\) | per-PRB | 0.9997 → 1.0000 | 83 → 0 TTIs |

#### Runtime envelope

Timing results show that grouped modes remain below the 1 ms compute budget through 64 UEs for every tested bandwidth on the measured host. At 256 UEs, grouped exceedance fractions are 0.0% at 20 MHz, 17.2–21.3% at 100 MHz, and 20.4–22.5% at 400 MHz. Distributed per-PRB 100 MHz already exceeds the budget in most TTIs at 16 UEs, and all tested 400 MHz per-PRB TTIs exceed it.

| 64-UE grid | P99 none | P99 rerank | Compute-budget exceedance none / rerank |
|---|---:|---:|---:|
| 20 MHz grouped | 188 µs | 192 µs | 0.0% / 0.0% |
| 100 MHz grouped | 364 µs | 377 µs | 0.0% / 0.0% |
| 400 MHz grouped | 333 µs | 379 µs | 0.0% / 0.0% |
| 20 MHz distributed per-PRB | 989 µs | 1,050 µs | 0.7% / 3.1% |
| 100 MHz distributed per-PRB | 5,454 µs | 5,778 µs | 100.0% / 100.0% |
| 400 MHz distributed per-PRB | 12,362 µs | 13,021 µs | 100.0% / 100.0% |

These measurements justify enabling allocation-unit reranking in the grouped reference profiles while retaining a configurable conservative default for high-resolution per-PRB studies. They do not establish end-to-end real-time application performance because process construction, packet capture, external application I/O, pacing, operating-system scheduling, and telemetry overhead are outside the timed interval.

### 8.5 Legacy BLER and HARQ behavior

All six 180-second profiles complete in disabled, active/no-retry, and active/four-retry modes under seed 20260927. First-attempt failures produce small throughput reductions in most profiles, while retransmissions measurably change RMa service because retry blocks consume grants that the disabled baseline uses for new payload.

| Profile and direction | Disabled | Active, no retry | Active, four retries |
|---|---:|---:|---:|
| RMa n78 DL | 52.09 Mbit/s | 50.96 Mbit/s | 50.07 Mbit/s |
| RMa n78 UL | 26.95 Mbit/s | 26.27 Mbit/s | 26.64 Mbit/s |
| UMa n78 DL | 103.80 Mbit/s | 102.49 Mbit/s | 102.71 Mbit/s |
| n258 high-loss DL | 235.16 Mbit/s | 231.99 Mbit/s | 231.52 Mbit/s |
| n258 high-loss UL | 55.31 Mbit/s | 54.31 Mbit/s | 54.97 Mbit/s |
| n258 nominal DL | 500.02 Mbit/s | 499.97 Mbit/s | 500.02 Mbit/s |

The active RMa result is slightly below the disabled arm in DL but close to it in UL. The new no-L4S baseline exhibits a much larger pending HARQ backlog than the earlier L4S-enabled evidence, which is consistent with removing an AQM intervention; however, the two evidence generations were not designed as a one-factor queue-mode experiment, so that difference is not assigned a causal effect here. The near-unchanged nominal n258 DL result is demand-limited. Retry behavior and queue occupancy must therefore be reported explicitly rather than hidden inside delivered throughput, and deployment-level interpretation remains blocked until the BLER and combining model is externally calibrated.

In the production arm with full telemetry, the largest aggregate retransmission rate is 1.14 Mbit/s in high-loss n258 DL, the largest radio-drop rate is 0.02 Mbit/s, and the largest sampled HARQ queue is 867 blocks in RMa DL. Oldest queued age reaches the configured 300 ms packet budget in the impaired cases. Across every direction, profile, and all three production seeds, the maximum absolute integer-bit conservation residual is zero. The two 30-second seed sweeps show retransmission maxima of 1.39 and 1.63 Mbit/s and queue maxima of 688 and 601 blocks; their shorter duration and warm-up make them sensitivity checks rather than direct long-run throughput estimates.

## 9. Current validity boundary

The current model is internally reproducible and dimensionally consistent for its declared abstractions, but the following limits prevent predictive deployment claims:

| Limitation | Consequence | Required extension |
|---|---|---|
| Propagation provenance | Stored map extent can exceed the measurement range behind an ABG fit | Consolidate source campaigns, validity ranges, heights, frequencies, and uncertainty |
| Single-cell interference surrogate | SINR does not respond to explicit neighbour geometry, beam state, scheduler activity, or penetration | Add staged multicell geometry and load/scheduling coupling |
| Scalar rank and MIMO | Rank has no channel matrix, precoder, receiver, layer SINR, or codeword mapping | Calibrate rank/layer state and then add matrix or lookup-based MIMO |
| Uncalibrated MCS and legacy BLER | Operational HARQ is reproducible but not predictive of a documented receiver | Generate link-level BLER data and calibrated link-to-system mapping |
| Limited statistical sampling | One principal packet seed and two short HARQ sensitivity seeds do not form a deployment distribution | Run predeclared seed and load ensembles |
| Incomplete cause classification | Queue/HARQ occupancy is visible, but every empty allocation is not classified by eligibility, backlog, scheduler, packet fit, or radio failure | Emit per-UE and per-allocation reason codes |
| Host-specific timing | Empirical runtime tails do not include external applications, packet capture, or operating-system guarantees | Repeat on target hosts with isolated and end-to-end timing |

Delivery metrics are sampled every 10 ms, so initial and final zero-delivery runs are lower bounds under censoring. These limitations constrain interpretation; they do not invalidate the deterministic comparisons reported in Section 8.

## 10. Planned model evolution

### 10.1 Multicell interference

The next interference model should separate reference-signal strength from traffic-dependent SINR without turning FikoRE into a site-planning simulator. A staged implementation would allow experiments to choose cost and fidelity explicitly:

| Stage | Added state | Intended use |
|---|---|---|
| Static neighbours | A small set of neighbour positions, carrier powers, scalar antenna gains, and path losses | Repeatable geometry-aware interference with fixed activity |
| Load-coupled neighbours | One utilization variable per cell and direction, updated from offered demand and available capacity | Capture the feedback between load, interference, and achievable rate |
| Scheduled neighbours | Per-resource neighbour activity and optional correlated TDD patterns | Study scheduler and resource-overlap effects |

A load-coupled intermediate model can represent the fixed point

\[
\rho_c=\sum_{u\in c}\frac{d_u}{W\log_2\!\left(1+\mathrm{SINR}_u(\boldsymbol{\rho})\right)},
\]

where \(\rho_c\) is the fraction of time-frequency resources used by cell \(c\), \(d_u\) is UE demand, and \(\boldsymbol{\rho}\) scales interference from all neighbours [12]. FikoRE could solve this equation once per slower load interval while retaining its 1 ms serving-cell scheduler. A later scheduled model would replace \(\rho_c\) with actual PRB/RBG overlap. Required design choices are neighbour count, wraparound or finite geometry, antenna and penetration treatment, DL/UL coupling, convergence policy, and the runtime budget per TTI.

### 10.2 Beam state and blockage

The existing n258 profiles use equivalent aligned scalar gains: they represent an already aligned FWA link and contain no beam search or blockage dynamics. A minimal explicit extension would give each link a transmit-beam identifier, receive-beam identifier, alignment state, and blockage state. The resulting link term could be

\[
G_{\mathrm{beam}}(t)=G_{\mathrm{tx}}(b_{\mathrm{tx}})+G_{\mathrm{rx}}(b_{\mathrm{rx}})-L_{\mathrm{align}}(a_t)-L_{\mathrm{block}}(z_t).
\]

The first implementation could use a finite-state process: aligned, misaligned, beam search, failure, and recovery. State transitions would be driven by mobility, measurement cadence, reporting delay, and optional blockage events; beam sweeps would consume explicit symbols or TTIs. Alignment and blockage should remain independent because a correctly selected beam can still be blocked, and a clear path can still be misaligned [13].

TR 38.901 provides two useful blockage precedents [1]. Model A is a computationally light stochastic attenuation process, while Model B represents explicit blocker geometry. A practical sequence is therefore to begin with spatially consistent stochastic blockage and fixed antenna-pattern tables, then add blocker geometry only for experiments that need body or vehicle trajectories. Absolute antenna gain, array-pattern gain, alignment loss, and blockage loss must be kept separate to avoid double-counting the scalar gains already present in the n258 profiles.

### 10.3 Carrier aggregation

Carrier aggregation must not be represented as one invalidly wide carrier because each component carrier can have a different frequency, bandwidth part, numerology, propagation state, interference process, CQI, MCS, HARQ process, and grant. The UE should retain one application-facing MAC/RLC queue hierarchy, while each serving cell owns a separate PHY and resource grid. The shared scheduler then decides both which UE and which carrier receives service [5], [14], [22]–[24].

An incremental implementation could start with downlink aggregation of two always-active component carriers and independent per-carrier scheduling. A second stage would add PCell/SCell activation, deactivation timers, bandwidth-part state, and optional cross-carrier scheduling. Uplink aggregation must additionally enforce one UE power budget across simultaneous carriers; assigning \(P_{\max}\) independently on each carrier would create non-physical power. TS 38.300 defines the serving-cell and carrier-aggregation architecture, while TS 38.213 provides the relevant power-control and overlapping-transmission constraints [3], [22].

Dual connectivity is a distinct extension rather than another CA flag: it introduces separate cell groups, potentially independent schedulers and timing, and explicit inter-group power sharing. Keeping CA and dual connectivity separate in the model avoids conflating one MAC entity over several carriers with two coordinated serving nodes.

### 10.4 MIMO

A practical next-stage MIMO abstraction should expose selected rank, post-processing SINR per layer, PRB, and codeword, power allocation, number of codewords, and MCS per codeword:

\[
\left(v,\Gamma_{\ell,k,\mathrm{cw}},P_{\ell,k},n_{\mathrm{cw}},m_{\mathrm{cw}}\right).
\]

In this tuple, \(v\) is selected rank, \(\Gamma_{\ell,k,\mathrm{cw}}\) is post-processing SINR for layer \(\ell\), resource \(k\), and codeword \(\mathrm{cw}\), \(P_{\ell,k}\) is layer power, \(n_{\mathrm{cw}}\) is codeword count, and \(m_{\mathrm{cw}}\) is codeword MCS. The scheduler consumes rank and per-codeword achievable rate, while the error model consumes the SINR vector, MCS, and transport-block context.

Two implementation levels are plausible. A calibrated lookup model would sample rank and layer-quality offsets conditioned on scenario, antenna configuration, mobility, and reference SINR; it is inexpensive and suitable when only application-facing capacity matters. A matrix model would generate a correlated channel, apply a codebook precoder and receiver, and derive post-processing SINR explicitly. The latter supports beam and MU-MIMO studies but requires antenna geometry, spatial correlation, precoder, receiver, feedback delay, and calibration data. The current scalar rank-one evidence should remain the baseline until either option is independently validated.

### 10.5 Link-to-system abstraction, HARQ, and OLLA

The operational legacy BLER path already provides retries and packet accounting; this section concerns its calibrated replacement. A predictive link-to-system abstraction requires reproducible AWGN BLER curves indexed by MCS, transport-block or code-block size, rank, receiver, and redundancy version. For a frequency-selective allocation, post-processing SINRs can be compressed per codeword with a calibrated effective-SINR mapping. For EESM,

\[
\gamma_{\mathrm{eff}}=-\beta_m\ln\!\left(\frac{1}{K}\sum_{k=1}^{K}e^{-\gamma_k/\beta_m}\right),
\]

where \(\gamma_k\) is post-processing SINR on resource \(k\) and \(\beta_m\) is an MCS-specific calibration parameter. The parameter is not a tuning constant chosen from system-level results: it should be fitted against link-level fading results so that effective SINR reproduces AWGN BLER for the same MCS [15], [16], [25]. MIESM is a modulation-aware alternative that maps SINR through mutual information before compression.

The effective SINR then indexes code-block BLER curves generated by an NR-compliant link-level simulator. Transport-block size determines LDPC base graph, lifting, and segmentation; code-block error probabilities are combined into transport-block error probability [25], [26]. This makes packet size and allocation shape part of the error model instead of relying on one BLER curve per MCS.

HARQ history depends on the combining method. Chase combining retains the coded transmission and combines SINR across attempts, whereas incremental redundancy also changes the accumulated effective code rate. 5G-LENA provides a concrete open implementation precedent for EESM calibration, LDPC segmentation, and both HARQ-CC and HARQ-IR [19], [25]. FikoRE should preserve its current bounded retry, timing, and packet-accounting contracts while replacing only the failure-probability calculation.

Outer-loop link adaptation should maintain a separate bounded belief offset that is updated from ACK/NACK outcomes toward a declared BLER target [17]. That offset belongs to the scheduler's link-quality belief; it must not shift reported physical SINR or RSRP. The present generic SINR offset moves both and is therefore unsuitable as OLLA state.

### 10.6 Calibration and observability

The highest-priority supporting work is calibration and observability rather than another unvalidated feature. The current implementation already exports queue and HARQ occupancy, retry ordinal, retransmitted and radio-dropped bits, charged grants, and exact packet-accounting residual.

| Missing evidence or signal | Why it matters | Proposed output |
|---|---|---|
| Consolidated ABG provenance | A map can be generated outside the measurement range of its fit | Coefficient table with campaign, frequency, distance, height, environment, and uncertainty |
| Independent radio measurements | Internal consistency does not establish field accuracy | Paired RSRP/SINR/MCS traces with synchronized position and configuration |
| Link-level BLER data | The legacy table cannot identify decoder or TBS behavior | Versioned curves and calibration metadata per MCS/TBS/rank/receiver |
| Empty-resource reason | A zero-delivery interval can have several causes | Per-UE reason taxonomy: no backlog, disabled, non-positive rate, rate cap, scheduler loss, retry-fit waste, expiry, or radio failure |
| Scheduler belief versus physical state | OLLA and control need estimated quality without corrupting observables | Separate reported SINR, scheduler SINR belief, OLLA offset, and ACK/NACK history |

These additions would let an experiment distinguish source idleness, sampled outage, scheduler competition, packet-fit waste, expiry, and radio decoding loss without inferring cause from throughput alone.

## 11. Conclusions

The current FikoRE physical layer provides a deterministic, computationally bounded abstraction of a single serving carrier. It combines spatially correlated large-scale gain, explicit environment and penetration state, resource-consistent signal and noise accounting, bounded uplink total power, threshold-based MCS and rank, TDD-aware resource accounting, generalized MT/PF/BET scheduling, and a hardened legacy BLER/HARQ packet path. This model is sufficient for controlled application-facing experiments in which relative effects and reproducibility are more important than exact receiver prediction.

The validation demonstrates deterministic map generation, correct local covariance behavior, reproducible packet-level scenarios, clear separation between TDD structure and resource assignment, strong service-continuity benefits from grouped-grid history reranking, exact packet and retry conservation, and a measured compute envelope that depends sharply on scheduling resolution. It also shows why assignment, charged transmission, effective payload, retransmission, and application delivery must be reported separately.

The model remains intentionally incomplete. Predictive use requires propagation-range provenance, measured calibration, explicit multicell interference, better rank and MIMO state, link-level BLER data, and a calibrated HARQ and OLLA chain. The planned evolution therefore prioritizes calibration and observability before adding additional unvalidated detail.

## Appendix A — Implementation and evidence traceability

This appendix records the exact software and evidence identity used for the quantitative results. It is not required to understand the physical model described in the main text.

### A.1 Document identity

| Item | Value |
|---|---|
| Document revision | 1.2 |
| Revision date | 2026-09-30 |
| Review status | External technical-review manuscript |
| Repository branch | `feature/phy-model-v2` |
| Release tag | Not assigned |

The document revision is assigned independently of Git because a document cannot stably contain the SHA of the same commit that introduces that SHA. A release tag may be assigned after review without changing the document contents.

### A.2 Software revisions

**Model implementation source:** `3094abdfc2546bfe8e9a7c57cb1f005e337a35b0`

**Packet-profile source:** `6770c645709517af5da8edf6ca9441f14c670374`

**Runtime-benchmark source:** `1b55ca191e27a368936476fff33fc622833d8d13`

**Scheduler-family benchmark source:** `6770c645709517af5da8edf6ca9441f14c670374`

| Component or campaign | Commit | Purpose |
|---|---|---|
| Final `dev` base | `325dfadd3809b2d7def25a980ef6782e1819ce48` | Persistent transport-connection update onto which the PHY feature was rebased |
| Integrated PHY/MAC baseline | `be8dcb4d550b8ce42d22640ed66c13c9139d0a08` | Power/noise, maps, O2I, UL finalization, and original validation framework |
| Deterministic map catalog | `278d4d5028ef6235e28ad60b4d593aea11dbe2aa` | Padded deterministic map-generation semantics |
| Generalized scheduler family | `a8dd44e7c3c5b654be88ec4f766f40276bc01db5` | MT/PF/BET shared recipe with independent aliases and RR |
| Scheduler characterization | `398ce5067f76931a38ae0d4d31e44df329d8522f` | Alias constraints, regression coverage, and neutral benchmark tooling |
| Hardened legacy BLER/HARQ | `e493ae3c0542dae7d0ce531c773cf543acf29928` | Active bounded lookup, retries, integer accounting, grants, timers, and capture verdict state |
| HARQ observability | `a4a5d2587a9e49b6e7af2cdc0129661692d0e6fc` | Exact conservation, queue, retry, and radio-loss telemetry |
| Final NFQUEUE closeout | `3094abdfc2546bfe8e9a7c57cb1f005e337a35b0` | Stop/drain/join teardown, overflow retry, fail-fast terminal errors, and saturation tests |
| Neutral study-queue defaults | `dabcf3261462bf32256321dedeb81334bbe7d0c0` | L4S disabled in the five basic offline study profiles and the basic live rural profile |
| Final packet and scheduler campaigns | `6770c645709517af5da8edf6ca9441f14c670374` | Repeatable campaign IDs and post-rebase evidence source |
| Original development baseline | `e995563fd3e8fa300f4b0accca3b494f7177101c` | Baseline before the PHY feature series |

The rebase rewrote feature commit identifiers but not their patches. The committed source map records each cited pre-rebase SHA, its reachable rebased equivalent, and a stable patch ID; historical run manifests retain their original source SHA rather than pretending that an old experiment ran on a new commit.

### A.3 Model and schema versions

| Item | Version or policy |
|---|---|
| Production map generator | `fikore-map-generator 2.1.0` |
| Production map semantics | `2.1.0` |
| Production map count | 21 |
| Production grid | Odd 291×291 with `explicit-center-cell` origin |
| Master map seed | `20260927` |
| Canonical UE environment key | `ue_location_type` |
| Nearest-frequency fallback | Disabled unless explicitly enabled |
| Default MIMO configuration | One antenna and one layer |
| General-purpose study queue | Legacy single queue; L4S enabled only by explicit comparison profiles |
| Evaluated HARQ/BLER decision | Active `legacy_bler`, production retry limit four; disabled and no-retry paired ablations |

### A.4 Experiment provenance

| Campaign | Source commit | Seed(s) | Duration or sample count | Date |
|---|---|---|---|---|
| Scheduler-family characterization | `6770c645709517af5da8edf6ca9441f14c670374` | Deterministic benchmark | 288 cases × 300 measured TTIs | 2026-09-30 |
| Paired HARQ disabled/no-retry/production campaign | `6770c645709517af5da8edf6ca9441f14c670374` | `20260927` | 3 modes × 6 profiles × 180 s, 20 s warm-up | 2026-09-30 |
| Production HARQ seed sweep | `6770c645709517af5da8edf6ca9441f14c670374` | `20260928`, `20260929` | 6 profiles × 30 s/seed, 2 s warm-up | 2026-09-30 |
| Map ensemble | `1b55ca191e27a368936476fff33fc622833d8d13` | Master seeds `20270000`–`20270029` | 30 realizations × 21 catalog entries | 2026-09-29 |
| Historical pre-HARQ packet profiles | `1b55ca191e27a368936476fff33fc622833d8d13` | `20260927` | 180 s/profile, 20 s warm-up | 2026-09-29 |
| Historical standalone n258 O2I pair | `1b55ca191e27a368936476fff33fc622833d8d13` | `20260927` | 180 s/arm, 20 s warm-up | 2026-09-29 |
| Legacy-v1 versus current-map pair | `1b55ca191e27a368936476fff33fc622833d8d13` | `20260927` | 180 s/profile, 20 s warm-up | 2026-09-29 |
| Per-TTI runtime envelope | `1b55ca191e27a368936476fff33fc622833d8d13` | Deterministic benchmark | 1,000 TTI samples/case | 2026-09-29 |
| Longer PF functional campaign | `1b55ca191e27a368936476fff33fc622833d8d13` | Deterministic benchmark | 500 warm-up + 2,000 measured TTIs/case | 2026-09-29 |

The validation host, recorded in manifests as `pitahaya`, used an AMD Ryzen 7 5800H with 8 physical cores and 16 logical CPUs, Linux 5.15.0-58, g++ 11.4.0, Python 3.10.4, NumPy 2.2.6, and Matplotlib 3.10.7. Runtime processes used one emulator thread and the host was not CPU-isolated.

### A.5 Validation and artifact record

| Artifact or check | Repository location |
|---|---|
| Unified evidence manifest | `docs/baselines/phy-model-v2-evidence-manifest.json` |
| Complete validation matrix and reproduction commands | `docs/baselines/phy-v2-validation-matrix.md` |
| Map catalog and map hashes | `include/maps_scenarios/CATALOG.json`, `include/maps_scenarios/MANIFEST.json` |
| Multi-seed map diagnostics | `docs/baselines/map-v2-multiseed-*.csv`, `docs/baselines/map-v2-multiseed-validation.md` |
| Disabled and production HARQ packet summaries used in Sections 8.2 and 8.5 | `docs/baselines/harq-final-v2/` |
| Historical pre-HARQ packet summaries | `docs/baselines/phy-v2-production-*.csv` |
| Current controlled O2I pair | `docs/baselines/harq-final-v2/disabled-*`, `docs/baselines/harq-final-v2/o2i-paired-sinr.csv` |
| Historical standalone O2I comparison | `docs/baselines/phy-v2-o2i-controlled-comparison.*` |
| Legacy/current catalog comparison | `docs/baselines/map-v2.1-paired-profile-comparison.*` |
| PF runtime and functional results | `docs/baselines/pf-runtime-*`, `docs/baselines/pf-functional-long*` |
| Throughput scheduler guide and benchmark | `docs/throughput-schedulers.md`, `tools/benchmark_scheduler_family.py` |
| Scheduler-family matrix | `docs/baselines/scheduler-family-final/` |
| HARQ contract, table provenance, and campaign evidence | `docs/harq-legacy-bler.md`, `docs/baselines/harq-final-v2/` |
| Frozen pre-rebase HARQ and scheduler evidence | `docs/baselines/harq-v2/`, `docs/baselines/scheduler-family-v2/` |
| Pre/post-rebase source mapping | `docs/baselines/final-rebase-source-map.json` |
| Figure-generation script | `tools/generate_phy_v2_figures.py` |
| Evidence verifier | `tools/verify_phy_v2_evidence.py` |
| Python evidence dependencies | `tools/requirements-phy-v2.txt` |

The final validation passed the complete C++ and map test suite, deterministic and soak HARQ tests, smoke scenarios, API tests, transport-model unit and integration tests, deterministic figure regeneration, validation of all 21 production maps, and recursive verification of the legacy and HARQ evidence packages. Raw packet logs remain local because of their size; exact portable inputs, source identifiers, summaries, commands, and hashes are committed, so experiments can be regenerated but the original raw samples cannot be independently audited from the repository alone.

### A.6 Revision history

| Revision | Main change |
|---|---|
| Legacy physical model | Unseeded legacy maps, mixed resource-bandwidth references, CQI-coupled PF history, and overloaded O2I state |
| Core PHY/MAC correction | Resource-consistent power and noise, allocation-aware UL power, generalized MT/PF/BET recipes, and TTI-updated service history |
| Initial deterministic maps | Seeded odd-grid maps, explicit origin, binary LOS generation, and exact 2.38 GHz UMi support |
| Current deterministic maps | Padded FFT embedding removes periodic edge seams; explicit provenance and exact-frequency lookup are enforced |
| Adversarial review corrections | Shared DL/UL environment state, normalized Rayleigh power, safe MIMO indexes, FR1/FR2 overhead correction, TDD-effective resource metrics, per-TTI timing, and portable evidence verification |
| Final development integration | Feature history rebased onto `dev` commit `325dfadd`, transport changes retained, source equivalence mapped, and evidence regenerated |
| Scheduler and HARQ recovery | Independent scheduler aliases share one implementation; the hardened legacy BLER mode is active with exact packet conservation and paired evidence |
| Neutral unattended defaults | Basic offline and live study UEs use the legacy single queue; L4S is opt-in through dedicated comparison profiles |

### A.7 Reproduction caveats

The evidence package separates implementation source, experiment source, and document revision. Historical manifests retain the commit that actually produced them, while the rebase source map identifies patch-equivalent reachable commits. The committed summaries and rendered inputs are hash-verified, but raw logs are not archived. Runtime results are host-specific; the principal packet comparison uses one seed and two shorter production sweeps provide sensitivity only. Map confidence intervals are pointwise across independent realizations and do not establish external propagation accuracy.

## References

1. 3GPP TR 38.901 v18.1.0, *Study on channel model for frequencies from 0.5 to 100 GHz*, Release 18, 2026.
2. 3GPP TS 38.104 v18.8.0, *NR; Base Station radio transmission and reception*, Release 18, 2025.
3. 3GPP TS 38.213 v18.7.0, *NR; Physical layer procedures for control*, Release 18, 2025.
4. 3GPP TS 38.214 v18.9.0, *NR; Physical layer procedures for data*, Release 18, 2026.
5. 3GPP TS 38.306 v18.8.0, *NR; User Equipment radio access capabilities*, Release 18, 2025.
6. J. M. Reyero Lobo, *Advancing 5G Network Emulation: Comprehensive Enhancements to FikoRE's Physical and MAC Layers*, Universidad Politécnica de Madrid, 2025.
7. S. Sun et al., “Investigation of Prediction Accuracy, Sensitivity, and Parameter Stability of Large-Scale Propagation Path Loss Models for 5G Wireless Communications,” *IEEE Transactions on Vehicular Technology*, vol. 65, no. 5, 2016, <https://doi.org/10.1109/TVT.2016.2543139>.
8. G. R. MacCartney, Jr. and T. S. Rappaport, “Study on 3GPP Rural Macrocell Path Loss Models for Millimeter Wave Wireless Communications,” *IEEE ICC*, 2017, <https://doi.org/10.1109/ICC.2017.7996793>.
9. C. Zhang, X. Chen, H. Yin, and G. Wei, “Two-Dimensional Shadow Fading Modeling on System Level,” *IEEE PIMRC*, 2012, <https://doi.org/10.1109/PIMRC.2012.6362617>.
10. F. P. Kelly, “Charging and Rate Control for Elastic Traffic,” *European Transactions on Telecommunications*, vol. 8, no. 1, 1997, <https://doi.org/10.1002/ett.4460080106>.
11. H. J. Kushner and P. A. Whiting, “Convergence of Proportional-Fair Sharing Algorithms Under General Conditions,” *IEEE Transactions on Wireless Communications*, vol. 3, no. 4, 2004, <https://doi.org/10.1109/TWC.2004.830826>.
12. I. Siomina and D. Yuan, “Analysis of Cell Load Coupling for LTE Network Planning and Optimization,” *IEEE Transactions on Wireless Communications*, vol. 11, no. 6, 2012, <https://doi.org/10.1109/TWC.2012.051512.111532>.
13. M. Giordani, M. Polese, A. Roy, D. Castor, and M. Zorzi, “A Tutorial on Beam Management for 3GPP NR at mmWave Frequencies,” *IEEE Communications Surveys & Tutorials*, vol. 21, no. 1, 2019, <https://doi.org/10.1109/COMST.2018.2869411>.
14. K. I. Pedersen et al., “Carrier Aggregation for LTE-Advanced: Functionality and Performance Aspects,” *IEEE Communications Magazine*, vol. 49, no. 6, 2011, <https://doi.org/10.1109/MCOM.2011.5783991>.
15. I. Latif, F. Kaltenberger, N. Nikaein, and R. Knopp, “Large Scale System Evaluations using PHY Abstraction for LTE with OpenAirInterface,” *SIMUTools*, 2013, <https://doi.org/10.4108/icst.simutools.2013.251738>.
16. B. Classon et al., “Efficient OFDM-HARQ System Evaluation Using a Recursive EESM Link Error Prediction,” *IEEE WCNC*, 2006, <https://doi.org/10.1109/WCNC.2006.1696579>.
17. A. Sampath, P. S. Kumar, and J. M. Holtzman, “On Setting Reverse Link Target SIR in a CDMA System,” *IEEE VTC*, 1997, <https://doi.org/10.1109/VETEC.1997.600465>.
18. C. Mehlführer et al., “The Vienna LTE Simulators—Enabling Reproducibility in Wireless Communications Research,” *EURASIP Journal on Advances in Signal Processing*, 2011, <https://doi.org/10.1186/1687-6180-2011-29>.
19. N. Patriciello, S. Lagen, B. Bojovic, and L. Giupponi, “An E2E Simulator for 5G NR Networks,” *Simulation Modelling Practice and Theory*, vol. 96, 2019, <https://doi.org/10.1016/j.simpat.2019.101933>.
20. G. Nardini et al., “Simu5G—An OMNeT++ Library for End-to-End Performance Evaluation of 5G Networks,” *IEEE Access*, vol. 8, 2020, <https://doi.org/10.1109/ACCESS.2020.3028550>.
21. D. González Morín, M. J. López-Morales, P. Pérez, A. García Armada, and Á. Villegas, “FikoRE: 5G and Beyond RAN Emulator for Application Level Experimentation and Prototyping,” *IEEE Network*, vol. 37, no. 4, pp. 48–55, 2023, <https://doi.org/10.1109/MNET.002.2200595>.
22. 3GPP TS 38.300 v18.3.0, *NR; NR and NG-RAN Overall Description; Stage-2*, Release 18, 2024.
23. 3GPP TS 38.321 v18.10.0, *NR; Medium Access Control (MAC) Protocol Specification*, Release 18, 2026.
24. 3GPP TS 38.331 v18.10.0, *NR; Radio Resource Control (RRC) Protocol Specification*, Release 18, 2026.
25. S. Lagén, K. Wanuga, H. Elkotby, S. Goyal, N. Patriciello, and L. Giupponi, “New Radio Physical Layer Abstraction for System-Level Simulations of 5G Networks,” *IEEE ICC*, 2020, <https://doi.org/10.1109/ICC40277.2020.9149444>.
26. 3GPP TS 38.212 v18.8.0, *NR; Multiplexing and Channel Coding*, Release 18, 2026.
