---
id: TASK-1
title: 'v0.2 "sees debt, pays debt, never regresses" — shipped; remaining candidates'
status: Done
assignee: []
created_date: '2026-08-20'
updated_date: '2026-08-21'
labels:
  - roadmap
dependencies: []
priority: high
ordinal: 1000
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
DECIDED 2026-08-20 with the owner and SHIPPED as **v0.2.0** (plan: `docs/plans/2026-08-20-001-feat-tag-debt-payoff-v0-2-plan.md`). The chosen set was items 1–4 of the candidate menu plus two picker parity debts:

- **Rename-with-asset-fixup** (the headline; the engine's own rename fixes nothing — UE-194640): plan/preview → redirects → consented referencer resave → verify → optional retirement, with a one-way state machine (rollback only before any save) and a skip-to-resave recovery path.
- **Audit → actions**: Delete Unused, Create Redirect…, Resave Referencers (multi-select, per-item outcomes, stale banner).
- **Browser usage counts** + sort-by-usage lens (explicit scan, three honest states).
- **Audit CI commandlet** + fail-closed wrapper (`scripts/run-tagtoolbox-tag-audit.ps1`).
- **Picker parity**: inline create-tag row; pill copy/paste.

Foundations shipped with it: the shared tag scan service (all-source redirect aggregation — fixed the v0.1 settings-only blind spot), the shared consented resave engine, and the destructive-flow engine characterization in `docs/architecture.md`.

## Remaining candidates (next selection — discuss before scoping)

1. **Function-parameter filters**: function-level `GameplayTagFilter` metadata; the Kismet function-customization hook mirrors the variable one already used.
2. **Dimension/exclusivity rules**: "children of X are mutually exclusive per container"; audit category + runtime `NormalizeByRules` + container-editor surfacing.
3. **Scriptable mutation seams** (from the v0.2 code review's agent-native pass): a non-interactive `ExecuteRename` policy overload (explicit dirty/retirement flags instead of dialogs), plus `TagToolbox.CreateRedirect <Old> <New>`, `TagToolbox.RunTagAudit`, and `TagToolbox.ScanTagUsage` console commands — all thin wrappers over already-pure functions.
4. Polish: split the >1000-line `STagToolboxTagPicker.cpp` (extract the create-row and usage-count units); Find-in-Blueprint deep-jump from References; dropdown search autofocus; a transiently-authored-redirect test for clipboard old-name resolution; restricted tag lists (`SourceRestrictedTagList`) are invisible to redirect collection/snapshots — record in the TASK-2 gate table.
<!-- SECTION:DESCRIPTION:END -->
