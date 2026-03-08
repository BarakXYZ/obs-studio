# Lenses Vision

## Purpose
Lenses is an OBS-based desktop video layer that enables realtime, automatic visual enhancement for on-screen content.

The first flagship capability is **Dark Mode Everything**:
- Detect bright and white-heavy regions on screen in realtime.
- Intelligently invert or remap those regions for dark-mode-friendly viewing.
- Preserve foreground subjects (for example, a lecturer over slides) so people and other protected content are not incorrectly inverted.
- Activate automatically only when configured thresholds are exceeded.

## Why This Matters
Many users consume content that is not optimized for visual comfort:
- Bright slide decks in lectures.
- White-background videos and web pages.
- Poorly encoded or unbalanced color in media.

Lenses gives users control over how content is displayed, without requiring the source content to change.

## Core Experience
Lenses runs in the background and mirrors desktop visuals through an OBS-powered processing layer.

When conditions are met, it applies selected effects in realtime:
1. Region detection and segmentation.
2. Selective filtering and color transforms.
3. Output compositing with minimal latency.

## Initial Feature Pillars
### 1) Dark Mode Everything
- Realtime white/brightness region detection.
- Subject-aware segmentation (avoid inverting people/foreground objects).
- Adaptive inversion and contrast-safe mapping.
- Threshold-based auto-enable behavior.

### 2) Automatic Color Intelligence
- Realtime color lift/augmentation/correction.
- "Repair" mode for visibly broken color encoding.
- Brightness reduction with selective highlight preservation.
- User-tunable strength and profiles.

### 3) Intelligent Object/Region Tools
- Detect and isolate key content regions.
- Apply effects per region, not globally.
- Support future extensible effects pipeline.

## Engineering Principles
- **Realtime first**: low-latency processing suitable for always-on use.
- **Selective, not destructive**: preserve faces/subjects/important overlays.
- **Predictable activation**: effects trigger only when confidence and thresholds are met.
- **User agency**: automatic modes with manual overrides.

## Repository and Upstream Strategy
To stay maintainable while tracking upstream OBS:

- Keep fork-specific functionality isolated in clearly named modules/plugins.
- Minimize invasive core modifications where possible.
- Prefer additive architecture:
  - `plugins/lenses-*` for new features.
  - dedicated docs under `docs/lenses/`.
  - master execution plan in `docs/lenses/plans/lenses-mask-first-master-plan.md`.
  - licensing/commercialization decision notes in `docs/lenses/licensing-and-commercialization.md`.
  - explicit feature flags/config boundaries.
- Rebase frequently onto upstream `master`.
- Use focused branches for each feature stream.

## Branching Convention (Proposed)
- `codex/lenses` for initial integration and planning.
- `codex/lenses-dark-mode-*` for Dark Mode Everything feature work.
- `codex/lenses-color-*` for color intelligence work.
- `codex/lenses-infra-*` for build/tooling/CI organization.

## Near-Term Milestones
1. Confirm local build and run flow from source.
2. Scaffold `lenses` plugin/module layout with minimal integration points.
3. Implement prototype white-region detector.
4. Add first-pass subject protection segmentation.
5. Ship MVP "Dark Mode Everything" toggle + auto-threshold mode.
6. Add early auto color correction prototype.

## Success Criteria (MVP)
- Stable realtime desktop layer on supported platforms.
- Dark Mode Everything improves readability/comfort on white-heavy content.
- Subject protection prevents obvious foreground artifacts.
- User can tune thresholds and effect intensity.
- Upstream sync remains manageable with low merge friction.
