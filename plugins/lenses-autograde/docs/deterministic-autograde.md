# Deterministic Autograde (No ML)

## Overview
The runtime is intentionally split into three modules:

1. `analysis.cpp`
2. `params.cpp`
3. `lut.cpp`

The pipeline is:

1. Analyze ROI in a deterministic way (histograms, clipping, white-balance cues, gradients).
2. Solve global grade parameters with an objective-driven search.
3. Synthesize a fused 3D LUT and constrain it toward identity for safety.

This produces a compact deterministic transform suitable for realtime shader application in OBS.

## Research Basis

- Contrast and intensity transforms:
  - OpenCV intensity transform docs.
- Local contrast enhancement:
  - CLAHE (Pizer et al., 1990).
- Automatic exposure objective framing:
  - Auto Exposure Correction of Consumer Photographs (ECCV 2012).
- White balance estimators:
  - Gray-world / shades-of-gray / gray-edge families (as used in OpenCV xphoto and classic color constancy literature).
- No-reference quality metrics (for future optional tuning targets):
  - BRISQUE / NIQE.

## Current Objective
Current parameter solving uses a deterministic objective over transformed ROI samples:

- target midtone placement (`p50`)
- target tonal spread (`p95 - p05`)
- entropy bonus
- clipping penalties (highlights/shadows)
- oversaturation penalty

Exposure is searched over a bounded range and selected by maximum objective score.

## Safety Constraints

- LUT is constrained toward identity with confidence-dependent max delta.
- WB/exposure/contrast/saturation/vibrance are bounded.
- HDR/extended input path remains blocked by filter host logic.

## Notes

- The output LUT domain remains compatible with OBS `LUTAmount3D` conventions:
  - sample UVW in nonlinear space
  - LUT entries stored as linear RGB.
