---
id: TASK-3
title: 'Fab packaging and listing'
status: To Do
assignee: []
created_date: '2026-08-20'
labels:
  - release
dependencies:
  - TASK-2
priority: medium
ordinal: 3000
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
Package via `RunUAT BuildPlugin` per supported engine version once TASK-2 lands, and prepare the Fab listing.

Notes from the 2026-08-19 competitive research:
- Closest competitor on the filters slice: "Blueprint Variable Metadata" (Fab, 5.0/5 from a small sample). "Typed Gameplay Tags" covers C++-typed filtering. Free open-source OUUTags covers audits.
- **Nobody found on Fab ships**: project-wide tag colors, picker favorites/recents, a bundled suite, or rename-with-fixup — those are the differentiators to lead the listing with.
- Manual browser pass still needed on competitor pricing (Fab blocks automated fetches).
- License is MIT (chosen 2026-08-20): Fab revenue positioning = convenience, prebuilt binaries, and support rather than exclusivity.
- Also verify the colored pill visually in a clean host project without Paper2DPlus during packaging QA (the one AC that could not be fully exercised in the development host before the chip handoff).
<!-- SECTION:DESCRIPTION:END -->
