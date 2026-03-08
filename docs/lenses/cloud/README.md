# Lenses Cloud Backend (Scaffold)

Phase-1 cloud support is implemented as a feature-gated `IMaskGenerator` backend that preserves the same `MaskFrame` contract used by local inference.

## Build Flag

- `LENSES_ENABLE_CLOUD` (default OFF)

When enabled, provider values prefixed with `cloud` instantiate the cloud adapter.

## Runtime Behavior

- Local-first behavior is preserved.
- Frames are still submitted through the local generator path.
- If cloud inference does not produce a result within timeout budget, local result is used.
- Policy engine and compositor do not change between local/cloud backends.

## Current Scope

- Contract + failover semantics are implemented.
- External network transport and auth are intentionally deferred.
- This keeps render path deterministic while we harden the interface.
