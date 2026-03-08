# Lenses Quality Audits

This directory contains lightweight, repository-local audit tooling for the
Lenses plugin architecture refactor track.

## Purpose
1. Keep objective snapshots of structural complexity before/after refactors.
2. Provide repeatable commands for file-size, function-size, and include-dependency audits.
3. Support CI integration later without introducing external tooling dependencies.

## Audits
1. `audit_file_sizes.py`
2. `audit_function_sizes.py`
3. `audit_include_deps.py`
4. `run_lenses_quality_audits.sh` (wrapper)

## Usage
Run from repository root:

```bash
tools/lenses-quality/run_lenses_quality_audits.sh docs/lenses/audits/<snapshot-dir>
```

This generates:
1. `file-sizes.json`
2. `function-sizes.json`
3. `include-deps.json`
4. `SUMMARY.md`

## Notes
1. Function-size parsing is heuristic and intended for trend analysis and gating support.
2. Generated reports exclude vendored/build trees by default.
