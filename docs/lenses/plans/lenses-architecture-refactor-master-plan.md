# Lenses Architecture Refactor Master Plan (Maintainability-First Foundation)

## Intent
This plan is a pure architecture and maintainability refactor track. The goal is to make the Lenses plugin modular, testable, and safe for aggressive future GPU/performance work without repeatedly destabilizing runtime behavior.

## Scope
1. In scope: decomposition, contracts, ownership boundaries, testability, quality gates, diagnostics interfaces.
2. Out of scope for this plan: changing model quality targets, changing product UX behavior, adding new model families.
3. Constraint: preserve runtime behavior during decomposition phases unless a phase explicitly declares a behavior change.

## Baseline Audit (Current Code)
1. First-party source LOC is concentrated in two files:
   - `plugins/lenses/src/filter/lenses-filter.c` (2890 LOC)
   - `plugins/lenses/src/ai/ort/onnx-mask-generator.cpp` (2028 LOC)
   - Combined: 4918/7797 LOC (63.1%) of `plugins/lenses/src`.
2. `lenses-filter.c` mixes too many concerns:
   - render loop orchestration, AI submission, policy graph compositing, debug overlays, OBS properties UI, runtime telemetry, model selection, config migrations.
   - oversized entry points include:
     - `lenses_filter_render` around `plugins/lenses/src/filter/lenses-filter.c:2092`
     - `lenses_filter_update` around `plugins/lenses/src/filter/lenses-filter.c:2294`
     - `lenses_filter_properties` around `plugins/lenses/src/filter/lenses-filter.c:2528`
3. `onnx-mask-generator.cpp` mixes too many concerns:
   - provider selection, ORT session creation, CoreML probing and telemetry parsing, I/O binding, preprocess, inference, decode, tracking, scheduler, queue policy, fallback behavior, stats and health.
   - oversized core methods include:
     - `InitializeSession` around `plugins/lenses/src/ai/ort/onnx-mask-generator.cpp:887`
     - `RunInference` around `plugins/lenses/src/ai/ort/onnx-mask-generator.cpp:1500`
     - `WorkerLoop` around `plugins/lenses/src/ai/ort/onnx-mask-generator.cpp:1734`
4. Policy runtime still carries compatibility shortcuts:
   - runtime rule stores both `class_id_count/class_ids` and legacy `class_id` first-element shortcut at `plugins/lenses/src/io/lenses-policy.c:361-365`.
5. Legacy/fallback logic is distributed across layers instead of centralized:
   - filter-level fallback pipeline handling and legacy policy writes in `lenses-filter.c`.
   - runtime fallback modes in `onnx-mask-generator.cpp`.
6. Test coverage is too narrow for current complexity:
   - existing tests cover decode, tracker, soak, benchmark only.
   - no direct tests for filter rendering orchestration, provider ladder decisions, policy parsing edge cases, or model catalog/runtime config compatibility.
7. Build/CI has no enforced modularity gates for this plugin:
   - no size/complexity/fan-in/fan-out guardrails to prevent regression back to monoliths.

## Design Principles
1. Single responsibility per module.
2. Explicit contracts between C filter host and C++ runtime domain.
3. Behavior-preserving refactor first, then controlled behavior upgrades.
4. Every refactor phase has measurable acceptance criteria.
5. No hidden fallback behavior; all fallback decisions are explicit and diagnosable.

## Target Architecture

### 1) Host Layer (OBS-facing, C)
Responsibilities:
1. OBS source lifecycle (`create`, `destroy`, `update`, `render`, `properties`, defaults).
2. Host-owned resource lifecycle for textures/stagesurfaces.
3. Translating OBS settings into runtime config DTOs.

Target files:
1. `plugins/lenses/src/filter/host/lenses-filter-host.c`
2. `plugins/lenses/src/filter/host/lenses-filter-render.c`
3. `plugins/lenses/src/filter/host/lenses-filter-settings.c`
4. `plugins/lenses/src/filter/host/lenses-filter-properties.c`
5. `plugins/lenses/src/filter/host/lenses-filter-debug.c`
6. `plugins/lenses/src/filter/host/lenses-filter-state.h`

### 2) Pipeline Layer (C)
Responsibilities:
1. Pass graph execution and texture ping-pong.
2. Policy mask composition.
3. Debug overlay compositing.

Target files:
1. `plugins/lenses/src/pipeline/graph/policy-graph.c`
2. `plugins/lenses/src/pipeline/graph/policy-graph.h`
3. `plugins/lenses/src/pipeline/overlay/debug-overlay.c`
4. `plugins/lenses/src/pipeline/overlay/debug-overlay.h`
5. keep `lenses-pass.c` focused as low-level pass primitive only.

### 3) Runtime Orchestration Layer (C++)
Responsibilities:
1. runtime state machine and queue ownership.
2. backend creation/selection.
3. health/stats surface.

Target files:
1. `plugins/lenses/src/core/runtime/core-runtime-controller.cpp`
2. `plugins/lenses/src/core/runtime/core-runtime-controller.hpp`
3. `plugins/lenses/src/core/runtime/runtime-config-mapper.cpp`
4. `plugins/lenses/src/core/runtime/runtime-config-mapper.hpp`

### 4) ORT Backend Layer (C++)
Responsibilities:
1. strictly backend-specific logic, split by domain.

Target files:
1. `plugins/lenses/src/ai/ort/session/ort-session-bootstrap.cpp`
2. `plugins/lenses/src/ai/ort/session/ort-provider-config.cpp`
3. `plugins/lenses/src/ai/ort/io/ort-io-binding.cpp`
4. `plugins/lenses/src/ai/ort/preprocess/ort-preprocess.cpp`
5. `plugins/lenses/src/ai/ort/infer/ort-inference-runner.cpp`
6. `plugins/lenses/src/ai/ort/postprocess/ort-segmentation-postprocess.cpp`
7. `plugins/lenses/src/ai/ort/scheduler/ort-worker-loop.cpp`
8. `plugins/lenses/src/ai/ort/telemetry/ort-runtime-metrics.cpp`
9. `plugins/lenses/src/ai/ort/onnx-mask-generator.cpp` retained only as thin facade.

### 5) Policy and Model I/O Layer (C)
Responsibilities:
1. policy schema parsing and runtime form.
2. legacy migration isolated from runtime parser.
3. model catalog loading/validation.

Target files:
1. `plugins/lenses/src/io/policy/policy-compiler.c`
2. `plugins/lenses/src/io/policy/policy-runtime-loader.c`
3. `plugins/lenses/src/io/policy/policy-legacy-compat.c`
4. `plugins/lenses/src/io/model/model-catalog-loader.c`
5. `plugins/lenses/src/io/model/model-catalog-validate.c`

## Quality Gates (Mandatory)
1. Max file size in hot-path modules: 800 LOC.
2. Max function size in hot-path modules: 120 LOC.
3. No function in host render path may perform policy parsing or provider selection.
4. No function in backend modules may call OBS property APIs.
5. `fallback` activation must emit structured reason string and backend state.
6. New modules require unit tests for failure paths and edge cases.

## Phase Program

### R00 - Guardrails and Baseline Snapshot
1. Add lightweight audit scripts:
   - file-size report
   - function-size report
   - include dependency report
2. Store baseline reports under `docs/lenses/audits/`.
3. Exit criteria:
   - baseline artifact committed
   - CI/manual command available for repeat audits.

### R01 - Split Host Filter Monolith
1. Extract from `lenses-filter.c`:
   - properties builder
   - settings update/config normalization
   - render loop orchestration
   - debug text/overlay generation
2. Keep behavior identical (no semantic changes).
3. Exit criteria:
   - old file under 900 LOC
   - no runtime behavior delta in smoke test scene.

### R02 - Split ORT Monolith by Domain
1. Extract session bootstrap/provider config into dedicated modules.
2. Extract preprocess, inference run, I/O binding, and worker scheduler.
3. Keep `OnnxMaskGenerator` as facade + coordination only.
4. Exit criteria:
   - `onnx-mask-generator.cpp` facade under 500 LOC
   - extracted modules compile independently and pass tests.

### R03 - Runtime Controller Boundary
1. Move queue management and frame-drain logic out of `core-bridge.cpp` implementation class.
2. Introduce `CoreRuntimeController` with deterministic lifecycle state transitions.
3. Exit criteria:
   - `core-bridge.cpp` reduced to C ABI adapter and DTO marshaling.

### R04 - Policy Runtime Semantics Cleanup
1. Remove first-class-id shortcut dependency in runtime matching path.
2. Keep compatibility loader for legacy policy data in isolated adapter.
3. Exit criteria:
   - schema-compliant selector semantics remain deterministic
   - legacy preset migration covered by dedicated tests.

### R05 - Model Catalog and Config Hardening
1. Split catalog loading from validation.
2. Add explicit compatibility check API: model shape/layout/runtime knobs.
3. Exit criteria:
   - invalid config/model combinations fail early with actionable diagnostics.

### R06 - Tests Expansion
1. Add unit tests for:
   - backend resolver ladders
   - runtime config normalization
   - policy parser edge cases
   - I/O binding mode decisions
2. Add integration smoke tests for:
   - filter create/update/render/destroy lifecycle
   - runtime health snapshot consistency.
3. Exit criteria:
   - new tests run in CI path used by this repo.

### R07 - CI Modularity Gates
1. Add CI checks for:
   - file size threshold
   - function size threshold
   - forbidden cross-layer include rules
2. Fail build on violations.
3. Exit criteria:
   - regression to monolith shape becomes impossible to merge.

### R08 - Observability Contract Consolidation
1. Standardize diagnostics snapshot schema across host/runtime/backend.
2. Ensure debug UI consumes this schema only (no scattered ad hoc formatting logic).
3. Exit criteria:
   - one authoritative diagnostics struct path from runtime to UI.

### R09 - Final Cleanup and ADRs
1. Remove dead compatibility code paths that are superseded.
2. Publish ADR set for module boundaries and ownership rules.
3. Exit criteria:
   - architecture docs match code tree
   - no hot-path module exceeds gates.

## Deliverables
1. Refactored code tree with clear boundaries.
2. CI quality gates that block monolith regressions.
3. Expanded test suite covering runtime correctness and configuration safety.
4. Architecture decision records for long-term maintainability.

## Definition of Done
1. No first-party hot-path source file exceeds 800 LOC.
2. `lenses-filter.c` and `onnx-mask-generator.cpp` are facades, not implementation monoliths.
3. Runtime/backend/policy/model concerns are isolated by module and test-covered.
4. Diagnostics and fallback semantics are explicit and deterministic.
5. Future GPU optimization work can be implemented in isolated modules without cross-layer churn.
