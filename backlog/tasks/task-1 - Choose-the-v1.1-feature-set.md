---
id: TASK-1
title: 'Choose the v1.1 feature set'
status: To Do
assignee: []
created_date: '2026-08-20'
labels:
  - roadmap
dependencies: []
priority: high
ordinal: 1000
---

## Description

<!-- SECTION:DESCRIPTION:BEGIN -->
The v1.1 selection is DELIBERATELY UNDECIDED — discuss before scoping. Candidate menu from the 2026-08-19 research and design discussions, roughly ranked:

1. **Rename-with-asset-fixup** (recommended headline): the engine's `RenameTagInINI` writes a `GameplayTagRedirects` entry but never resaves referencing assets, and there is no native way to see which assets used the old name (open Epic issue UE-194640). We already have the exact referencing-package set from Asset Registry searchable-name data. Shape: rename dialog → engine rename → load + resave exactly the referencers → re-check referencers → offer to retire the redirect.
2. **Audit → actions**: "Delete selected unused tags" (`IGameplayTagsEditorModule::DeleteTagsFromINI` is public), "Create redirect…" for undefined tags, "Resave referencers" for lingering redirects.
3. **Browser usage counts**: per-row referencer counts from a cached audit scan; sort-by-usage lens.
4. **Audit CI commandlet**: headless run that fails on referenced-but-undefined tags.
5. **Function-parameter filters**: the remaining filter-coverage hole (function-level `GameplayTagFilter` metadata; the Kismet module has a function-customization hook mirroring the variable one already used).
6. **Dimension/exclusivity rules**: declare "children of X are mutually exclusive per container"; enforce via audit category, runtime `NormalizeByRules` helper, and optionally live in the container editor.
7. Polish: Find-in-Blueprint deep-jump from the References pane, dropdown search autofocus on open, ScriptCallable settings functions for automation.

Items 1–3 form one coherent story ("Tag Toolbox doesn't just show tag debt, it pays it down") and would demo well for the MegaGrant application (TASK-4).
<!-- SECTION:DESCRIPTION:END -->
