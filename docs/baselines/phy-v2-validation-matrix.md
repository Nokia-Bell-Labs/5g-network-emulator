# PHY Model V2 Validation Matrix

## Executed gates

| Area | Evidence | Result |
|---|---|---|
| Final `dev` integration | Rebase source map; transport unit and integration suites | 67 feature commits rebased onto `325dfadd`; stable patch IDs preserved; persistent-connection transport tests passed |
| Production map determinism | Two independently generated 21-map catalogs; production regeneration after diagnostics refactor | All map files and `CATALOG.json` byte-identical |
| V2.1 seam correction | Padded FFT generation; multi-seed opposite-edge diagnostic | Largest absolute ensemble-mean opposite-edge correlation 0.026 |
| Manifest integrity | `build_manifest.py --check`; `validate_maps.py` | 21 v2 entries, metadata and hashes consistent |
| Origin/interpolation | `map_handler_v2_test` | Explicit centre cell and axial lookup passed |
| Exact n40 lookup | `profile_config_test` | 2.38 GHz UMi map selected exactly |
| Configuration migration | `ue_location_type_config_test` | Canonical values passed; removed `location` key rejected; pre-feature `o2i` warning path retained |
| Neutral study queues | `profile_config_test`; five offline profiles plus basic live rural profile | Every general-purpose study/background UE defaults to `l4s_dual_queue: false`; dedicated comparison profiles remain explicit |
| Allocation bandwidth | `phy_power_model_test` | Integrated signal/noise SINR invariant for 1/2/4/8/16-PRB grouping |
| UL total power | `phy_power_model_test`; `ul_power_finalization_test` | Per-PRB reconstruction conserved total power for 1–275 PRBs; 23 dBm cap respected |
| MIMO table indexing | `phy_mimo_index_test` | One-based layer/rank state mapped safely to zero-based 1–4-layer tables |
| Throughput scheduler family | `pf_metric_test`; `pf_state_test`; `pf_scheduler_test` | MT, BET, and PF recipes; policy-driven history; CQI-cadence invariance; tie rotation; priority; RR independence passed |
| Scheduler migration | `pf_config_migration_test` | Removed PF-specific history keys rejected; `beta_metric` rejected for PF/BET; alias-specific options enforced |
| Reranking/granularity | post-rebase 288-case scheduler-family matrix | Zero failed cases; grouped PF reranking materially reduced service gaps; MT and RR remained outside provisional history |
| Legacy BLER contract | `harq_contract_test`; table hash analysis | All table axes bounded; historical law characterized; 67,200-value table hash and monotonicity fixed |
| HARQ packet path | `harq_pdcp_test`; `pdcp_flow_test`; `harq_soak_test`; ASan/UBSan/TSan | Exact integer conservation, bounded retries, independent deadlines/RNGs, grant conservation, concurrent captured verdict retry/shutdown, and one-million-decision replay passed |
| HARQ profile comparison | Post-rebase six-profile disabled/no-retry/production campaign plus two extra seeds | All modes completed with neutral study queues; maximum absolute closure residual remained zero |
| TDD resource accounting | `tdd_resource_metrics_test`; per-TTI grid summaries | Directionally unavailable units excluded before empty/fill metrics |
| Building/vehicle loss | `penetration_model_test`; `phy_shared_environment_test`; controlled n258 high-loss profile | Formula, shared DL/UL realization, independent keyed streams, and paired behavior passed |
| Canonical end-to-end profiles | Five 180 s runs plus n258 high-loss, seed `20260927`, 20 s warm-up analysis | All completed; post-rebase summaries and exact rendered inputs committed without altering historical evidence |
| Legacy/v2.1 map ablation | Same code/seed profiles with explicit legacy-v1 and padded-v2.1 catalogs | One-factor profile deltas committed |
| Map ensemble | 30 master seeds × 21 catalog entries | 630 realizations; LOS, shadow, autocorrelation, link-gain CIs reported |
| Runtime envelope | 10 repeats × 100 individually timed TTIs after 20 warm-up TTIs | 1,000 TTI samples/case; P50/P95/P99/max and 1 ms compute-budget exceedance fraction recorded |
| Longer PF function | 500 warm-up + 2,000 measured TTIs at 64 UEs | Both modes converge near Jain 1; reranking reduces maximum delivery gaps |
| Complete build | `make test`; dashboard test; `make smoke` | Passed |

## Commands

```bash
python3 -m pip install -r tools/requirements-phy-v2.txt
make -j4 test
make -j4 smoke
FIKORE_RUN_PRIVILEGED_NFQUEUE=1 make test-nfqueue
python3 tools/maps/build_manifest.py --check
python3 tools/maps/validate_maps.py
python3 tools/maps/validate_v2_statistics.py \
  --realizations 30 \
  --master-seed-base 20270000 \
  --output results/map-v2-multiseed
python3 tools/run_phy_v2_profiles.py \
  --seed 20260927 \
  --duration-s 180 \
  --output results/phy-v2-production-maps
python3 tools/analyze_phy_v2_profiles.py \
  results/phy-v2-production-maps/manifest.json \
  --warmup-s 20 \
  --output results/phy-v2-production-analysis
mkdir -p results/legacy-v1-maps-dc81da6
git archive dc81da62f59ca93e36a02c36bdcecd45ec6cd2bd \
  include/maps_scenarios \
  | tar -x -C results/legacy-v1-maps-dc81da6 \
      --strip-components=2
python3 tools/run_phy_v2_profiles.py \
  --seed 20260927 \
  --duration-s 180 \
  --map-dir results/legacy-v1-maps-dc81da6 \
  --output results/phy-v2-legacy-paired
python3 tools/run_phy_v2_profiles.py \
  --profiles offline_umi_n258_fwa_high_loss \
  --seed 20260927 \
  --duration-s 180 \
  --output results/phy-v2-o2i-high-loss
python3 tools/benchmark_scheduler_family.py \
  --schedulers bet,max_throughput,round_robin,pf \
  --ue-counts 16,64 \
  --envelope-modes \
  --warmup-steps 20 \
  --steps 100 \
  --repeats 3 \
  --output results/scheduler-family-final
python3 tools/publish_scheduler_evidence.py \
  results/scheduler-family-final \
  --output docs/baselines/scheduler-family-final
python3 tools/benchmark_scheduler_family.py \
  --schedulers bet,pf \
  --ue-counts 64 \
  --envelope-modes \
  --warmup-steps 500 \
  --steps 2000 \
  --repeats 1 \
  --output results/pf-functional-long
python3 tools/run_harq_campaign.py \
  --seed 20260927 \
  --duration-s 180 \
  --batch-id-prefix phy-final \
  --output results/harq-final
python3 tools/publish_harq_evidence.py \
  --campaign disabled=results/harq-final/disabled \
  --campaign legacy-no-retry=results/harq-final/legacy-no-retry \
  --campaign legacy-production=results/harq-final/legacy-production \
  --output docs/baselines/harq-final-v2
python3 tools/analyze_legacy_bler_table.py \
  --expect-sha256 d61acbe2a5cea399570c53b40f0374261ca28ac80ccefed0aee85728ea9bda70
python3 tools/generate_phy_v2_figures.py
python3 tools/verify_phy_v2_evidence.py
```

## Interpretation boundaries

- Unit and deterministic tests establish internal consistency, not external
  channel-model validity.
- The map ensemble confidence unit is the independent realization, never the
  spatial cell.
- The five profiles are common-seed characterizations, not confidence
  intervals over traffic, mobility, or deployment uncertainty.
- General-purpose study UEs use the legacy single queue. Historical evidence
  with an L4S-enabled rural study UE is retained but not mixed into the final
  packet tables.
- The runtime benchmark uses approximately 4 Gbit/s aggregate offered traffic
  per direction. The earlier 100 Gbit/s-per-UE stress input caused artificial
  unbounded packet-queue growth and was discarded as an invalid runtime
  experiment.
- Timing statements are limited to the measured, non-isolated host and exact
  grid/population combinations. Reported P99 values are empirical.
- Raw logs remain local because of their size; committed inputs, hashes,
  manifests, summaries, and analysis definitions permit regeneration but not
  independent auditing of the original raw samples.
