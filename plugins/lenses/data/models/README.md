# Bundled Lenses Models

This directory contains model packages discoverable by the Lenses in-app model catalog.

## Bundled size tiers

This public fork keeps the built-in model catalog lightweight and GitHub-friendly by bundling
the `n` and `s` packages in-repo. Larger `m`, `l`, and `x` packages should be installed into
the user model root (`models/`) so the runtime can discover them without inflating clone size or
crossing GitHub's large-file guardrails.

When a requested quality tier is not installed, the runtime falls back to the nearest available
tier, preferring an equal-or-smaller package before stepping up to a larger one.

## Package contract

Each package should be a subdirectory with at least:

- `model.onnx`
- `metadata.json`
- `class-map.json`

Recommended metadata fields:

- `id`
- `name`
- `size_tier` (`n`, `s`, `m`, `l`, `x`) for quality routing
- `class_count`
- `model.dynamic` / `model.static_input` / `model.static_output`
- `model.supports_iobinding_static_outputs`

For strict GPU runtime gates, metadata should match real model I/O semantics. If a
package declares `dynamic: true` while the ONNX graph has static `imgsz`, runtime
configuration and provider options can become suboptimal or inconsistent.

Lenses scans both:

- bundled path: `plugins/lenses/data/models`
- user path: module config `models/`

User packages with the same `id` override bundled packages.
