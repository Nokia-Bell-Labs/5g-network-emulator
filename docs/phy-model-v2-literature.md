# PHY Model V2 Literature Set

## Scope and citation policy

This curated set supports the formal model, validation protocol, and open
Phase 3 alternatives in the PHY Model V2 paper. Standards citations identify a
version and clause. Paper citations include a DOI verified against a publisher,
institutional record, or the article itself. A reference supports only the
specific statement listed below; it is not evidence that FikoRE implements the
complete cited model.

Numbering in this topical working set is local to this document. The
manuscript's numbered bibliography is authoritative and intentionally includes
the FikoRE thesis as a separate source.

## Normative and calibration references

1. **3GPP TR 38.901 v18.1.0, Release 18**, *Study on channel model for
   frequencies from 0.5 to 100 GHz*. Use Clauses 7.3 for antenna modeling,
   7.4.2 for LOS probability, 7.4.3 for O2I penetration, and 7.6.3 for spatial
   consistency. Stable ETSI document:
   <https://www.etsi.org/deliver/etsi_tr/138900_138999/138901/18.01.00_60/tr_138901v180100p.pdf>.

2. **3GPP TS 38.104 v18.8.0, Release 18**, *NR; Base Station radio
   transmission and reception*. Use Clauses 5.3 and 5.3A for channel bandwidth
   and carrier aggregation, Clause 6.2 for BS output power, and Clause 6.3 for
   power dynamics. Stable ETSI document:
   <https://www.etsi.org/deliver/etsi_ts/138100_138199/138104/18.08.00_60/ts_138104v180800p.pdf>.

3. **3GPP TS 38.213 v18.7.0, Release 18**, *NR; Physical layer procedures for
   control*. Use Clause 7.1.1 for PUSCH power control and the total-power cap.
   Stable ETSI document:
   <https://www.etsi.org/deliver/etsi_ts/138200_138299/138213/18.07.00_60/ts_138213v180700p.pdf>.

4. **3GPP TS 38.214 v18.9.0, Release 18**, *NR; Physical layer procedures for
   data*. Use Clause 5.1.3 for downlink MCS, code rate, layers, and TBS;
   Clause 5.2.2.1 for CQI; Clause 5.2.2.5.1 for CQI/PMI/RI assumptions; and
   the corresponding Clause 6 procedures for PUSCH. Stable ETSI document:
   <https://www.etsi.org/deliver/etsi_ts/138200_138299/138214/18.09.00_60/ts_138214v180900p.pdf>.

5. **3GPP TS 38.306 v18.8.0, Release 18**, *NR; User Equipment radio access
   capabilities*. Use Clauses 4.2.7.1 and 4.2.7.4–4.2.7.8 for supported band
   combinations, NR carrier aggregation, per-CC feature sets, and layer
   capabilities. Stable ETSI document:
   <https://www.etsi.org/deliver/etsi_ts/138300_138399/138306/18.08.00_60/ts_138306v180800p.pdf>.

## Propagation, LOS, O2I, and spatial fields

6. S. Sun *et al.*, “Investigation of Prediction Accuracy, Sensitivity, and
   Parameter Stability of Large-Scale Propagation Path Loss Models for 5G
   Wireless Communications,” *IEEE Transactions on Vehicular Technology*,
   vol. 65, no. 5, pp. 2843–2860, 2016,
   <https://doi.org/10.1109/TVT.2016.2543139>. This is both an ABG source and
   an explicit warning that ABG parameters can be less stable under
   extrapolation than CI/CIF parameters.

7. G. R. MacCartney, Jr. and T. S. Rappaport, “Study on 3GPP Rural Macrocell
   Path Loss Models for Millimeter Wave Wireless Communications,” *IEEE ICC*,
   2017, <https://doi.org/10.1109/ICC.2017.7996793>. Use to motivate explicit
   RMa range and height limitations; it does not validate FikoRE's retained
   RMa ABG coefficients.

8. C. Zhang, X. Chen, H. Yin, and G. Wei, “Two-Dimensional Shadow Fading
   Modeling on System Level,” *IEEE PIMRC*, pp. 1671–1676, 2012,
   <https://doi.org/10.1109/PIMRC.2012.6362617>. Supports filtered 2D shadow
   fields and interpolation, subject to independent validation of the exact
   FikoRE filter normalization.

## Scheduling and interference

9. F. P. Kelly, “Charging and Rate Control for Elastic Traffic,” *European
   Transactions on Telecommunications*, vol. 8, no. 1, pp. 33–37, 1997,
   <https://doi.org/10.1002/ett.4460080106>. Defines the proportional-fair
   utility criterion; it does not prescribe FikoRE's finite-window
   implementation.

10. H. J. Kushner and P. A. Whiting, “Convergence of Proportional-Fair Sharing
    Algorithms Under General Conditions,” *IEEE Transactions on Wireless
    Communications*, vol. 3, no. 4, pp. 1250–1259, 2004,
    <https://doi.org/10.1109/TWC.2004.830826>. Supports rate-over-history PF
    under time-varying channels and motivates separating instantaneous rate
    from service history.

11. I. Siomina and D. Yuan, “Analysis of Cell Load Coupling for LTE Network
    Planning and Optimization,” *IEEE Transactions on Wireless
    Communications*, vol. 11, no. 6, pp. 2287–2297, 2012,
    <https://doi.org/10.1109/TWC.2012.051512.111532>. Provides a fixed-point
    basis for a lightweight load-coupled multicell-interference option.

## Beamforming, carrier aggregation, and MIMO

12. M. Giordani, M. Polese, A. Roy, D. Castor, and M. Zorzi, “A Tutorial on
    Beam Management for 3GPP NR at mmWave Frequencies,” *IEEE Communications
    Surveys & Tutorials*, vol. 21, no. 1, pp. 173–196, 2019,
    <https://doi.org/10.1109/COMST.2018.2869411>. Defines beam sweeping,
    measurement, reporting, alignment, and tracking costs that a scalar antenna
    gain cannot represent.

13. K. I. Pedersen *et al.*, “Carrier Aggregation for LTE-Advanced:
    Functionality and Performance Aspects,” *IEEE Communications Magazine*,
    vol. 49, no. 6, pp. 89–95, 2011,
    <https://doi.org/10.1109/MCOM.2011.5783991>. Motivates separate
    component-carrier state, activation, scheduling, and power accounting
    rather than representing CA as one wider carrier.

14. MIMO rank, codebook, and layer behavior should be anchored primarily to
    TS 38.214 Clauses 5.2.2.2 and 5.2.2.5.1 and TS 38.306 per-CC feature-set
    capabilities. The current FikoRE scalar rank-threshold model has no
    independent citation or calibration set and must be described as an
    approximation, not as standards-equivalent MIMO.

## Link abstraction, OLLA, and HARQ

15. I. Latif, F. Kaltenberger, N. Nikaein, and R. Knopp, “Large Scale System
    Evaluations using PHY Abstraction for LTE with OpenAirInterface,”
    *SIMUTools/Emutools Workshop*, 2013,
    <https://doi.org/10.4108/icst.simutools.2013.251738>. Provides a documented
    EESM/MIESM calibration and validation workflow and demonstrates why
    per-MCS link-level calibration is mandatory.

16. B. Classon *et al.*, “Efficient OFDM-HARQ System Evaluation Using a
    Recursive EESM Link Error Prediction,” *IEEE WCNC*, pp. 1860–1865, 2006,
    <https://doi.org/10.1109/WCNC.2006.1696579>. Supports an optional recursive
    effective-SINR HARQ abstraction; it is not evidence for FikoRE's current
    packet-level retransmission approximation.

17. A. Sampath, P. S. Kumar, and J. M. Holtzman, “On Setting Reverse Link
    Target SIR in a CDMA System,” *IEEE VTC*, vol. 2, pp. 929–933, 1997,
    <https://doi.org/10.1109/VETEC.1997.600465>. Foundational feedback-driven
    outer-loop target adjustment; an NR OLLA implementation still requires
    explicit BLER target, step-size, cadence, and convergence validation.

## Simulator calibration and reproducibility

18. C. Mehlführer *et al.*, “The Vienna LTE Simulators—Enabling
    Reproducibility in Wireless Communications Research,” *EURASIP Journal on
    Advances in Signal Processing*, 2011:29,
    <https://doi.org/10.1186/1687-6180-2011-29>. Supports public model
    definitions, link-to-system calibration, and reproducible experiment
    scripts.

19. N. Patriciello, S. Lagen, B. Bojovic, and L. Giupponi, “An E2E Simulator
    for 5G NR Networks,” *Simulation Modelling Practice and Theory*, vol. 96,
    101933, 2019, <https://doi.org/10.1016/j.simpat.2019.101933>. Provides a
    relevant open NR system-simulator architecture and 3GPP calibration
    precedent.

20. G. Nardini *et al.*, “Simu5G—An OMNeT++ Library for End-to-End Performance
    Evaluation of 5G Networks,” *IEEE Access*, vol. 8,
    pp. 181176–181191, 2020,
    <https://doi.org/10.1109/ACCESS.2020.3028550>. Provides another
    application-to-PHY abstraction boundary and scenario-validation reference.

21. D. González Morín, M. J. López-Morales, P. Pérez, A. García Armada, and
    Á. Villegas, “FikoRE: 5G and Beyond RAN Emulator for Application Level
    Experimentation and Prototyping,” *IEEE Network*, vol. 37, no. 4,
    pp. 48–55, 2023,
    <https://doi.org/10.1109/MNET.002.2200595>. Defines the original emulator,
    intended application-level boundary, and positioning that this V2 work
    refines rather than replaces.

22. **3GPP TS 38.300, Release 18**, *NR; NR and NG-RAN Overall Description;
    Stage-2*. Normative architecture reference for serving cells, carrier
    aggregation, and cell-group behavior.

23. **3GPP TS 38.321, Release 18**, *NR; Medium Access Control (MAC) Protocol
    Specification*. Normative reference for one MAC entity, logical-channel
    prioritization, serving-cell activation, grants, and HARQ entities.

24. **3GPP TS 38.331, Release 18**, *NR; Radio Resource Control (RRC) Protocol
    Specification*. Normative reference for PCell/SCell and serving-cell/BWP
    configuration.

## Use in the paper

- References 1–5 define terminology and dimensional constraints, not emulator
  compliance.
- References 6–8 support the propagation design discussion and its
  limitations.
- References 9–11 support the PF and future interference option matrix.
- References 12–14 define the missing state in the beamforming, CA, and MIMO
  options.
- References 15–17 support calibrated link adaptation rather than uncalibrated
  threshold replacement.
- References 18–20 support artifact manifests, controlled calibration, and
  reproducibility claims.
- Reference 21 anchors the original FikoRE architecture and novelty boundary.
- References 22–24 anchor the proposed NR carrier-aggregation state boundary.
