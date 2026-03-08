# Lenses Next-Gen Strict Audit and Remediation Plan (Perf-First, Plugin-Centric, Bundle-All Models)

1. All code should follow best practice state-of-the-art practices. Even if it will take much much more time, prefer to constantly refactor and adjust architecture to match state-of-the-art best practices standards instead of ad hoc inline solutions!
2. Properly research each pattern you implementing online to follow best practices. Don't invent the wheel where not needed!
3. You don't stop until you finish ALL phases completely! You should commit as you go in each checkpoint.
4. All code should be extremely scalable and maintanble in terms of folder and file structure. Never implement things in ad hoc ways. Always prefer and organized scalable folder and file structure with consolidated logic and modules!
5. Before you commit, double check your work and strictly audit it to detect and obvious or non-obvious but much needed refactors.
6. Strive to be elegant, state-of-the-art, efficient, optimal, idiomatic and robust. Never ever ad hoc and dirty. Always production ready code.
7. Other agents are working in parallel. Make sure to always commit only files you've been working on and don't touch files you have nothing to do with.

## Summary
This plan replaces the previous “all phases complete” posture with an evidence-based remediation program.  
It is optimized for your chosen decisions:

1. Delivery priority: **Perf first**.
2. Architectural boundary: **Plugin-centric** with thin frontend adapters.
3. Model distribution: **Bundle all model sizes (`n/s/m/l/x`)** with zero manual browsing UX.
4. Model-shape policy (current track): **Dynamic-shape models stay default for compatibility and iteration speed**.
5. Runtime determinism policy: **Strict fail if any CPU fallback is detected**.

Goal state: sustained real-time AI cadence (>=12 FPS target on M4 Max with selected model/input settings), no avoidable frame drops, strict modular architecture (no 2k+ monoliths), and release-gated quality/perf evidence.

---

## Strict Audit Findings (Current State)

### A. Critical Performance and Correctness Findings
1. **Monolith files are still too large and violate maintainability goals**: [lenses-filter.c](/Users/barakxyz/personal/desktop-avatar/obs-studio/plugins/lenses/src/filter/lenses-filter.c) is 2369 LOC, and [onnx-mask-generator.cpp](/Users/barakxyz/personal/desktop-avatar/obs-studio/plugins/lenses/src/ai/ort/onnx-mask-generator.cpp) is 1508 LOC.
2. **AI ingest path still includes expensive GPU->CPU readback + copies on the render path** in [lenses-filter.c:1471](/Users/barakxyz/personal/desktop-avatar/obs-studio/plugins/lenses/src/filter/lenses-filter.c:1471) to [lenses-filter.c:1600](/Users/barakxyz/personal/desktop-avatar/obs-studio/plugins/lenses/src/filter/lenses-filter.c:1600).
3. **Frame data is copied multiple times**: BGRA buffer copy in [core-bridge.cpp:354](/Users/barakxyz/personal/desktop-avatar/obs-studio/plugins/lenses/src/core/core-bridge.cpp:354), then queue copy in [onnx-mask-generator.cpp:382](/Users/barakxyz/personal/desktop-avatar/obs-studio/plugins/lenses/src/ai/ort/onnx-mask-generator.cpp:382).
4. **Inference worker applies additional sleep throttling** in [onnx-mask-generator.cpp:1242](/Users/barakxyz/personal/desktop-avatar/obs-studio/plugins/lenses/src/ai/ort/onnx-mask-generator.cpp:1242), while producer is already cadence-throttled; inference: this can suppress effective completion FPS under load.
5. **Queue defaults remain extremely tight** (`submit=2`, `output=1`) in [lenses-filter.c:52](/Users/barakxyz/personal/desktop-avatar/obs-studio/plugins/lenses/src/filter/lenses-filter.c:52), which increases drop pressure during spikes.
6. **Bundled model metadata is dynamic-shape oriented** (`dynamic: true` in all bundled packages). This is the current intentional compatibility posture, but it reduces static-output optimization opportunities and demands strict runtime gating.
7. **Policy runtime is not semantically complete**: runtime selection still pivots on first class ID path in [lenses-policy.c:193](/Users/barakxyz/personal/desktop-avatar/obs-studio/plugins/lenses/src/io/lenses-policy.c:193), diverging from full selector intent.
8. **Phase-complete ledger is overstated versus validation depth** in [lenses-mask-first-master-plan.md:20](/Users/barakxyz/personal/desktop-avatar/obs-studio/docs/lenses/plans/lenses-mask-first-master-plan.md:20) without corresponding full E2E perf/visual gates.

### B. Architectural and Upstream-Maintenance Findings
1. **Frontend coupling is still high** across [OBSBasic.cpp](/Users/barakxyz/personal/desktop-avatar/obs-studio/frontend/widgets/OBSBasic.cpp) and [OBSBasic_Preview.cpp](/Users/barakxyz/personal/desktop-avatar/obs-studio/frontend/widgets/OBSBasic_Preview.cpp).
2. **Header-only controller implementation** in [lenses-main-mirror-controller.hpp](/Users/barakxyz/personal/desktop-avatar/obs-studio/frontend/utility/lenses-main-mirror-controller.hpp) is not ideal for compilation boundaries and merge hygiene.
3. **Bundle size is very large by default** due to bundled models (`~479MB` under `plugins/lenses/data/models`) and full data copy behavior in [plugins/lenses/CMakeLists.txt:124](/Users/barakxyz/personal/desktop-avatar/obs-studio/plugins/lenses/CMakeLists.txt:124).

### C. Validation and Release-Gate Findings
1. Unit tests exist but do not yet prove production behavior for full ONNX E2E inference and visual composition regressions.
2. Benchmark executable exists, but no enforced CI gate currently validates the real OBS render+inference path KPIs on target hardware.

---

## Root-Cause Model for “12 FPS configured, 7-10 FPS observed”
1. **Likely primary bottleneck** (inference): model tier and input resolution vs CoreML partitioning and per-frame inference cost.
2. **Likely secondary bottleneck** (pipeline overhead): readback+copy chain plus CPU preprocess before ORT `Run`.
3. **Likely tertiary bottleneck** (scheduler pressure): tight queues and extra worker-side sleeping.
4. **GPU utilization not high does not imply no bottleneck**: EP partitioning can offload only part of graph while CPU-side preprocess/readback and orchestration remain limiting.

---

## Target Architecture (Decision Complete)

1. Keep `plugins/lenses` as the ownership center for AI, policy, compositor, and runtime orchestration.
2. Keep frontend changes as thin adapters only; no heavy logic in OBS widget files.
3. Split monoliths into focused modules with strict ownership boundaries and measurable complexity limits.
4. Enforce explicit performance budgets and release-gated evidence.
5. Preserve “no manual model file browsing” UX and keep bundled model dropdown only.
6. Bundle all model tiers in release builds, while enabling dev profiles to build minimal packs for iteration speed.
7. Enforce strict runtime determinism: any CPU EP fallback marks the run invalid for production baselines and blocks startup in strict mode.

---

## Public/Internal Interface Changes (Required)

1. Replace `lenses_policy_rule_runtime.class_id` with selector-capable runtime representation in [lenses-policy.h](/Users/barakxyz/personal/desktop-avatar/obs-studio/plugins/lenses/include/lenses/io/lenses-policy.h) to support multiple class IDs, class names, track IDs, confidence and area constraints.
2. Extend `RuntimeConfig` in [interfaces.hpp](/Users/barakxyz/personal/desktop-avatar/obs-studio/plugins/lenses/include/lenses/core/interfaces.hpp):
   - `preprocess_mode` (`scalar`, `simd`, `accelerate`),
   - `scheduler_mode` (`producer_timed`, `worker_timed`, `adaptive`),
   - `drop_policy` (`drop_oldest`, `drop_newest`, `block_never`),
   - `profiling_enabled`,
   - `stage_budget_ms` thresholds.
3. Extend `MaskGeneratorStats` with rolling rates and stage percentiles:
   - `submit_fps`, `complete_fps`, `drop_fps`,
   - `readback_ms_p50/p95/p99`,
   - `preprocess_ms_p50/p95/p99`,
   - `infer_ms_p50/p95/p99`,
   - `decode_ms_p50/p95/p99`,
   - `track_ms_p50/p95/p99`.
4. Add `ModelDescriptor` fields for optimization compatibility:
   - `static_input`, `static_output`,
   - `supports_iobinding_static_outputs`,
   - recommended input sizes and expected tier envelopes.
5. Add a strict C API snapshot getter in [core-bridge.h](/Users/barakxyz/personal/desktop-avatar/obs-studio/plugins/lenses/include/lenses/core/core-bridge.h) for one-call debug status to avoid scattered copy calls from UI.

---

## Implementation Program (Perf-First, Commit-by-Checkpoint)

## P0. Audit Baseline and Instrumentation Hardening
1. Add a reproducible benchmark harness for real filter path with fixed scenes and scripted runs.
2. Enable ORT profiling toggles and CoreML compute-plan profiling option exposure.
3. Add structured telemetry logs with one-line machine-parse format and periodic summary.
4. Define baseline KPI capture template committed under `docs/lenses/perf/benchmarks/`.

**Exit criteria**
1. Baseline run artifacts exist for `n/s/m` at `512` and `960x540`.
2. Every run records per-stage percentiles and FPS rates.

## P1. Hot Path Optimization Without Behavior Change
1. Remove double-throttle behavior by making scheduler single-owner (`producer_timed` default).
2. Introduce move-only frame handoff and reusable buffer pool to remove avoidable copies.
3. Keep readback double-buffering but add readiness-aware map strategy and reduced map stalls.
4. Replace scalar preprocess with Accelerate/vImage-backed path on macOS, retain scalar fallback.

**Exit criteria**
1. `complete_fps` improvement is statistically validated against P0 baseline.
2. Render thread frame-time regressions do not increase at p99.

## P2. ORT/CoreML Determinism and Dynamic-Model Compatibility (Current Track)
1. Keep bundled models dynamic-shape in this track; defer static-shape export work to a later optimization milestone.
2. Keep CoreML provider options explicit (`ModelFormat`, `MLComputeUnits`, `RequireStaticInputShapes`, `SpecializationStrategy`, cache directory).
3. Enforce strict runtime checks so sessions fail fast when CPU fallback is detected (`cpu_ep_fallback_detected=1`), instead of silently running degraded paths. In ORT session config this maps to `session.disable_cpu_ep_fallback=1`.
4. Keep dynamic-output I/O binding path deterministic and well-instrumented, including explicit logs of effective input dimensions and active provider state.
5. Add startup validation that logs effective provider configuration, coverage, and strict-gate decisions.

**Exit criteria**
1. Runtime reports deterministic provider capability state and strict-gate decisions for every startup.
2. Any run with CPU fallback in strict mode is blocked and marked invalid for baseline comparison.
3. At least one default production tier (`s`, 512) achieves sustained >=12 complete FPS on target hardware profile in a valid GPU-only run.

## P3. Monolith Decomposition (Maintainability Gate)
1. Split [lenses-filter.c](/Users/barakxyz/personal/desktop-avatar/obs-studio/plugins/lenses/src/filter/lenses-filter.c) into:
   - `lenses-filter-lifecycle.c`
   - `lenses-filter-properties.c`
   - `lenses-filter-render.c`
   - `lenses-filter-ai-submit.c`
   - `lenses-filter-debug.c`
   - `lenses-filter-policy.c`
2. Split [onnx-mask-generator.cpp](/Users/barakxyz/personal/desktop-avatar/obs-studio/plugins/lenses/src/ai/ort/onnx-mask-generator.cpp) into:
   - `ort-session.cpp`
   - `ort-worker.cpp`
   - `ort-preprocess.cpp`
   - `ort-infer.cpp`
   - `ort-postprocess.cpp`
   - `onnx-mask-generator.cpp` facade only.
3. Set complexity guardrails: no source file >800 LOC, no function >120 LOC unless justified in ADR.

**Exit criteria**
1. Build and runtime behavior parity maintained.
2. File-size and complexity checks pass in CI.

## P4. Policy Semantics Completion
1. Implement full selector semantics in runtime path, not only compile-time metadata.
2. Support multi-class include/exclude, class names, track IDs, confidence and area constraints.
3. Preserve deterministic priority/tie-break ordering and default-rule fallback.
4. Add migration compatibility from existing saved presets.

**Exit criteria**
1. Rule behavior matches schema intent for multi-selector scenarios.
2. Golden tests for overlap/priority cases pass.

## P5. Plugin-Centric Boundary Refactor (Upstream-Friendly)
1. Move heavy mirror orchestration to dedicated adapter classes under `frontend/utility/lenses/`.
2. Convert [lenses-main-mirror-controller.hpp](/Users/barakxyz/personal/desktop-avatar/obs-studio/frontend/utility/lenses-main-mirror-controller.hpp) to `.hpp + .cpp`.
3. Minimize `OBSBasic*` diffs to toggle routing and thin callbacks only.
4. Add an internal boundary doc mapping “OBS-owned vs Lenses-owned responsibilities”.

**Exit criteria**
1. Lenses-specific logic in `OBSBasic*.cpp` reduced to thin integration points.
2. Upstream rebase conflict surface is materially smaller.

## P6. Build/Packaging Posture
1. Keep all bundled models for release per decision.
2. Introduce build profiles:
   - `LENSES_MODEL_PACK=full` (release default),
   - `LENSES_MODEL_PACK=dev` (n/s only for local iteration).
3. Replace always-copy full data behavior with deterministic resource sync target that avoids stale runtime assets and unnecessary rebuild cost.
4. Add packaged-model manifest integrity check at startup.

**Exit criteria**
1. Release bundle behavior unchanged for end-users.
2. Dev iteration speed improved and stale-plugin/resource mismatch eliminated.

## P7. Debug and Observability Completion
1. Promote debug inspector from text-only status to actionable runtime panel content:
   - effective model/provider,
   - selector hits per rule,
   - instance counts and IDs,
   - composition order,
   - stage timings and queue health.
2. Add one-click “capture diagnostics snapshot” to file.
3. Add runtime warnings with explicit remediation hints for common misconfigurations.

**Exit criteria**
1. User can verify model activity and rule application from UI without reading raw logs.
2. Crash triage package includes everything needed for first-pass diagnosis.

## P8. Quality, Stability, and Release Gate
1. Add E2E ONNX integration tests using bundled test clips and expected class-mask outcomes.
2. Add visual golden tests for policy composition.
3. Add long soak tests with model/preset switching and memory-growth assertions.
4. Enforce release gate script that fails on unmet KPIs.

**Exit criteria**
1. All release gates pass.
2. New “state-of-the-art readiness checklist” is green and versioned.

---

## Test Cases and Scenarios (Must Exist Before Release)

1. **Performance**: `s@512`, `m@512`, `s@960x540` on CoreML path with strict mode enabled and `cpu_ep_fallback_detected=0`; record submit/complete/drop FPS and stage p99 latencies.
2. **Backpressure**: synthetic burst submit with bounded queue and deterministic drop policy verification.
3. **Policy correctness**: multi-rule overlap with include/exclude, tie-break determinism, default-rule fill.
4. **Tracking**: persistent IDs under occlusion/re-entry scenarios.
5. **Debug fidelity**: UI stats must match runtime counters within tolerance.
6. **Crash resilience**: malformed model metadata, missing output shapes, provider init failures, and strict-gate rejection behavior when CPU fallback would occur.
7. **Mirror integration**: click-through and exclusion behavior remains stable with main OBS window visible.
8. **Soak**: 8-hour continuous run with no unbounded memory growth and no deadlocks.

---

## Acceptance KPIs (Release Blocking)

1. `complete_fps >= 12.0` for default production profile on target machine/profile.
2. `drop_fps <= 1.0` sustained under default profile.
3. Render thread p99 <= 20ms under target scenario.
4. No source file >800 LOC in `plugins/lenses/src` after decomposition.
5. No known crash in startup/filter-open path with bundled models and default scene collection.
6. Full deterministic policy test suite pass.
7. Debug inspector displays non-empty live stats when inference is active.
8. Production baseline runs are valid only when `cpu_ep_fallback_detected=0` and strict runtime checks are enabled.

---

## Assumptions and Defaults (Locked)
1. Platform priority remains macOS-first.
2. Full model bundle (`n/s/m/l/x`) is shipped by default in production packages.
3. No manual file browsing required for normal model selection.
4. Plugin-centric ownership is enforced; frontend remains thin adapter.
5. Perf optimization does not weaken deterministic policy behavior.
6. Backward compatibility for existing filter settings and presets is mandatory.
7. Dynamic-shape model artifacts remain the default shipping posture for the current optimization track.
8. CPU fallback is disallowed in production baseline runs under strict mode.

---

## Primary External References Used
1. [ONNX Runtime CoreML Execution Provider](https://onnxruntime.ai/docs/execution-providers/CoreML-ExecutionProvider.html)
2. [ONNX Runtime I/O Binding](https://onnxruntime.ai/docs/performance/tune-performance/iobinding.html)
3. [ONNX Runtime Thread Management](https://onnxruntime.ai/docs/performance/tune-performance/threading.html)
4. [ONNX Runtime Profiling Tools](https://onnxruntime.ai/docs/performance/tune-performance/profiling-tools.html)
5. [Ultralytics Export Mode](https://docs.ultralytics.com/modes/export/)
6. [Ultralytics Track Mode](https://docs.ultralytics.com/modes/track/)
7. [Ultralytics YOLO11 Models](https://docs.ultralytics.com/models/yolo11/)
8. [OBS Plugin Template](https://github.com/obsproject/obs-plugintemplate)

## More References and Repos
1. Local OBS fork: `/Users/barakxyz/personal/desktop-avatar/obs-studio`.
2. Local Ultralytics repo: `/Users/barakxyz/personal/desktop-avatar/ultralytics`.
3. Local OBS Background Removal repo (OBS-native ORT/CMake patterns): `/Users/barakxyz/personal/desktop-avatar/obs-backgroundremoval`.
4. Ultralytics docs (export): <https://docs.ultralytics.com/modes/export/>.
5. Ultralytics docs (tracking): <https://docs.ultralytics.com/modes/track/>.
6. Ultralytics ONNX integration: <https://docs.ultralytics.com/integrations/onnx/>.
7. ONNX Runtime docs: <https://onnxruntime.ai/docs/>.
8. ByteTrack paper repo: <https://github.com/FoundationVision/ByteTrack>.
9. BoT-SORT repo: <https://github.com/NirAharon/BoT-SORT>.
10. OBS Background Removal architecture patterns (local CMake + ORT + mask processing) from local cloned repo.
