# Lenses Release Checklist

## Functional Gates

- [ ] Legacy invert filter still initializes and renders without errors.
- [ ] Policy preset load/reload/save works from filter properties.
- [ ] Deterministic policy order is preserved (priority desc, id asc).
- [ ] Multi-chain example policy is validated (`cars grayscale`, `people invert`, `default red`).
- [ ] Runtime stats render in filter UI without crashes.

## AI/Mask Gates

- [ ] Segmentation decode test passes.
- [ ] ByteTrack-style tracker test passes.
- [ ] Persistent track IDs remain stable across short occlusion windows.
- [ ] Class-union masks are produced for class-based routing.

## Performance Gates (M4 Max Baseline)

- [ ] `lenses-benchmark` executed and metrics captured.
- [ ] 1080p mirror render remains responsive with AI enabled.
- [ ] No render-thread blocking introduced by inference/runtime path.

## Stability Gates

- [ ] `lenses-runtime-soak-test` passes.
- [ ] No unbounded queue growth under overload.
- [ ] No crashes through repeated enable/disable cycles.
- [ ] Plugin unloads without orphaned sources or memory leak reports.

## Cloud/Backend Gates

- [ ] Local backend works when cloud backend is disabled.
- [ ] Cloud provider path falls back to local on timeout.
- [ ] Cloud fallback counters appear in runtime stats.

## Docs and Ops

- [ ] Model packaging docs are current (`docs/lenses/models/*`).
- [ ] Cloud contract docs are current (`docs/lenses/cloud/*`).
- [ ] Licensing/commercialization notes reviewed before release.
