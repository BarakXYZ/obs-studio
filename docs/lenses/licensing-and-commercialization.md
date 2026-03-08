# Lenses AI Licensing and Commercialization Notes

Status: working draft for experimentation  
Last updated: 2026-02-28

## Why This Exists
We want to move fast on model integrations (including Ultralytics as a candidate) without losing legal and commercialization clarity.

This document captures our current assumptions and decision gates so we can:
- keep prototyping now;
- make a deliberate production choice later;
- avoid last-minute licensing surprises.

This is an engineering planning note, not legal advice.

## Current Product/Business Assumptions
- Lenses is intended to remain fully open source.
- We may accept donations.
- We may offer paid hosted cloud compute features (for example, cloud-generated visuals/filters).
- Ultralytics is a candidate for testing, not yet a final dependency choice.

## Current Repo License Context (OBS Fork)
- The repo states GPL v2 or later (for example in `README.rst` and source headers).
- This gives flexibility, but compatibility must still be checked for every shipped dependency and module.
- Some subcomponents may carry narrower terms; final release must audit the exact shipped binary set.

## Ultralytics Licensing Snapshot (Checked 2026-02-28)
- Ultralytics provides AGPL-3.0 and Enterprise licensing paths.
- Their public license messaging says AGPL is the default open-source path.
- They also state AGPL expectations apply to larger works that include their code/models.
- AGPL includes network-use obligations (source availability to users interacting over a network).

## What "Fully Open Source + Commercial" Means Here
Possible under strong copyleft, if we comply:
- Donations: allowed.
- Paid distribution/support: allowed.
- Paid hosted service: allowed.

But obligations likely include:
- publishing corresponding source for covered components and modifications;
- providing source access to remote users where required;
- preserving required notices and license texts.

## Decision Options (Production)
1. AGPL path with Ultralytics:
   - Keep project/cloud stack fully copyleft-compliant.
   - Accept reciprocal source obligations as product policy.
2. Enterprise path with Ultralytics:
   - Commercial license from Ultralytics for proprietary flexibility.
   - Still keep our own open-source policy where we choose, but no AGPL lock-in from Ultralytics.
3. Non-Ultralytics permissive stack:
   - Prefer MIT/BSD-style model/runtime components to reduce copyleft coupling.

## Experiment Policy (Now)
Allowed now:
- local spikes and evaluation branches;
- performance/quality testing;
- interface prototyping around model providers.

Not allowed before release decision:
- public distribution that assumes unresolved license compatibility;
- shipping cloud features without a defined source-offer/compliance flow.

## Release Gate Checklist (Before Shipping)
1. Dependency inventory:
   - model code, inference runtime, pretrained checkpoints, conversion scripts, helper tools.
2. License matrix:
   - each dependency license, obligations, and compatibility with the shipped combination.
3. Distribution compliance:
   - include required notices and license files in app packages and repo.
4. Cloud compliance:
   - define and test user-visible source access flow if required.
5. Reproducibility:
   - document build and model provenance for every shipped artifact.
6. Final legal review:
   - confirm go/no-go path before first public release.

## Open Questions
- Will production choose AGPL-complete posture or enterprise/permissive path?
- Which model assets/checkpoints are approved for redistribution?
- Do we keep one unified license policy or per-feature policy for cloud modules?

## Canonical References
- Ultralytics licensing page: https://www.ultralytics.com/license
- Ultralytics repository license context: https://github.com/ultralytics/ultralytics
- GNU AGPLv3 text: https://www.gnu.org/licenses/agpl-3.0.en.html
- GNU GPL FAQ (selling/commercial use): https://www.gnu.org/licenses/gpl-faq.en.html
- GNU license compatibility list: https://www.gnu.org/licenses/license-list.en.html

## Maintenance Rule
Re-check external license terms before any release candidate, because vendor terms can change over time.
