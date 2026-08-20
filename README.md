# Tag Toolbox

A quality-of-life suite for Unreal Engine's Gameplay Tags, aimed at the gap between "tags are the backbone of modern UE projects" and "the editor gives designers almost no tooling for them."

**Status: pre-release (0.1.0, in development).** Developed against UE 5.8; broader engine-range support is planned.

## Install

Clone (or submodule) this repository into your project's `Plugins/` folder as `Plugins/TagToolbox`, then enable **Tag Toolbox** in the Plugins browser (or add it to your `.uproject`). A C++ project is required to compile the plugin; binaries are not distributed from this repository.

```
git clone https://github.com/InfiniteGameWorksTeam/TagToolbox.git Plugins/TagToolbox
```

## Features (v1)

| Feature | What it does |
|---------|--------------|
| **Blueprint variable tag filters** | Select any Blueprint-defined `GameplayTag` / `GameplayTagContainer` / `GameplayTagQuery` variable and set a picker filter (e.g. `Combat.Ability`) right in the variable's Details — the same `Categories` metadata C++ programmers use, finally reachable by designers. Works for arrays, sets, and map values too. |
| **Tag styles** | A project-wide tag → color registry (Project Settings → Plugins → Tag Toolbox). Colors resolve with ancestor fall-up: style `Combat` once and every `Combat.*` tag inherits it until a child overrides. Drives a colored-pill replacement for the stock tag property widget. |
| **Tag Browser** | A dockable window showing the whole tag tree with registry colors, live search, favorites, and recents — built for projects whose tag trees have outgrown the Project Settings list. |
| **Tag Audit** | A no-asset-load report built from Asset Registry data: tags that are defined but never referenced, tags that are referenced but no longer defined, and near-duplicate (case-variant) definitions — with click-through to referencing assets. |

## Design rules

- **Presentation, never authority.** Styles and filters change what designers *see and pick*, never what tags *mean*. Nothing in this plugin runs in a cooked game (the runtime module only carries the settings object).
- **Override-list registry.** The style registry is empty by default and every surface keeps its stock fallback — installing the plugin changes nothing until you author a style.
- **Filters are guidance, not enforcement.** A `Categories` filter constrains the picker; Blueprint logic and copy-paste can still set anything. Pair filters with project validation when correctness matters.

## Roadmap

- Tag rename/move with automatic `GameplayTagRedirects` authoring
- Dimension/exclusivity rules ("at most one `Context.*` per container") with a validator and a runtime normalize helper
- Tagged graph comments: give Blueprint comment nodes a tag so comment groups share a registry color project-wide
- Tag tree documentation export (markdown with dev comments)
- Cross-version engine support and Fab packaging

## Modules

| Module | Type | Purpose |
|--------|------|---------|
| `TagToolbox` | Runtime | `UTagToolboxSettings` — the tag styles registry and plugin options. No gameplay behavior. |
| `TagToolboxEditor` | Editor | Variable tag-filter customization, colored tag pill, Tag Browser, Tag Audit. |
