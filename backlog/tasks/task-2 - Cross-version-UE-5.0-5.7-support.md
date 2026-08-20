---
id: TASK-2
title: 'Cross-version UE 5.0–5.7 support'
status: To Do
assignee: []
created_date: '2026-08-20'
labels:
  - engine-compat
dependencies: []
priority: medium
ordinal: 2000
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
v1 targets UE 5.8 only. Known version boundaries, verified against installed engine source (see docs/architecture.md for the full seam facts):

- `SGameplayTagPicker`, `SGameplayTagCombo`, `SGameplayTagContainerCombo` are public only from **5.3**; the version-stable 5.0–5.8 factory is `IGameplayTagsEditorModule::MakeGameplayTagWidget` / `MakeGameplayTagContainerWidget`.
- `FCoreDelegates::GetOnPostEngineInit()` is the **5.8** spelling; earlier engines use the bare `OnPostEngineInit` member. Gate BOTH the add and remove sites or one side fails `-WarningsAsErrors`.
- `FBlueprintEditorModule::RegisterVariableCustomization` on **5.0** returns void, unregisters without a handle, and stores one customization per field class in a TMap (silent clobber risk) — registering for a narrower field class reduces the collision surface there.
- The engine's native "Gameplay Tag Roots" authoring row exists only on **5.7+**, so our reproduction of it is the sole route on 5.0–5.6.
- `FGameplayTag::GetTagLeafName` is **5.6+**; below that, parse the last dot segment.
- 5.0's `FGameplayTagNode` exposes no public DevComment/source accessors — route through `UGameplayTagsManager::GetTagEditorData`, whose signature also drifts (single-source 5.0 vs dual overloads 5.8).

Acceptance shape: compile + headless tests green on each engine in the supported range, with the per-version fallbacks (stock tag widgets below 5.3) documented in the README.
<!-- SECTION:DESCRIPTION:END -->
