# Tag Toolbox

A quality-of-life suite for Unreal Engine's Gameplay Tags, aimed at the gap between "tags are the backbone of modern UE projects" and "the editor gives designers almost no tooling for them."

**Status: pre-release (0.2.0, in development).** Developed against UE 5.8; broader engine-range support is planned.

## Install

Clone (or submodule) this repository into your project's `Plugins/` folder as `Plugins/TagToolbox`, then enable **Tag Toolbox** in the Plugins browser (or add it to your `.uproject`). A C++ project is required to compile the plugin; binaries are not distributed from this repository.

```
git clone https://github.com/InfiniteGameWorksTeam/TagToolbox.git Plugins/TagToolbox
```

## Features

| Feature | What it does |
|---------|--------------|
| **Blueprint variable tag filters** | Select any Blueprint-defined `GameplayTag` / `GameplayTagContainer` / `GameplayTagQuery` variable and set a picker filter (e.g. `Combat.Ability`) right in the variable's Details — the same `Categories` metadata C++ programmers use, finally reachable by designers. Works for arrays, sets, and map values too. |
| **Tag styles** | A project-wide tag → color registry (Project Settings → Plugins → Tag Toolbox). Colors resolve with ancestor fall-up: style `Combat` once and every `Combat.*` tag inherits it until a child overrides. Drives a colored-pill replacement for the stock tag property widget. |
| **Enhanced pickers** | The pill's dropdown adds search, favorites, one-click recents, colored rows, **inline tag creation** (type a missing name, create it in place — filter-aware), and **copy/paste** with cause-naming refusals. Container properties get a collapsible chip strip. |
| **Tag Browser** | A dockable window showing the whole tag tree with registry colors, live search, favorites, recents, a per-tag **References** pane (no loads), and **usage counts** with a sort-by-usage lens — honest three-state badges that never show a confident wrong number. |
| **Tag rename with asset fix-up** | The flow the engine never shipped (see UE-194640): rename a tag or subtree, preview every referencing package, write the redirects, resave the referencers under explicit consent (unsaved edits survive), verify the old names are gone, and retire the redirects — with rollback that can never fire after a package saved. |
| **Tag Audit → actions** | The no-asset-load report (unused / undefined / near-duplicate / redirect issues) now fixes what it finds: delete unused tags, create redirects for orphaned references, and resave lingering referencers — all confirmed, all reported per item. |
| **CI tag gate** | `-run=TagToolboxTagAudit` + `scripts/run-tagtoolbox-tag-audit.ps1`: a schema-versioned JSON report with a fail-closed wrapper that fails builds on referenced-but-undefined tags (the shipped-bug class) before they ship. |

## Design rules

- **Presentation, never authority.** Styles and filters change what designers *see and pick*, never what tags *mean*. Nothing in this plugin runs in a cooked game (the runtime module only carries the settings object).
- **Override-list registry.** The style registry is empty by default and every surface keeps its stock fallback — installing the plugin changes nothing until you author a style.
- **Filters are guidance, not enforcement.** A `Categories` filter constrains the picker; Blueprint logic and copy-paste can still set anything. Pair filters with project validation when correctness matters.
- **Read paths never load; fix-ups load under confirm.** Audits, counts, and references are Asset Registry metadata only. The rename and resave actions are the sanctioned loaders — explicit, consented, per-package reported.

## Roadmap

- Dimension/exclusivity rules ("at most one `Context.*` per container") with a validator and a runtime normalize helper
- Function-parameter tag filters; scriptable (non-modal) entry points for the rename and audit actions
- Tag tree documentation export (markdown with dev comments)
- Cross-version engine support and Fab packaging

## Modules

| Module | Type | Purpose |
|--------|------|---------|
| `TagToolbox` | Runtime | `UTagToolboxSettings` — the tag styles registry and plugin options. No gameplay behavior. |
| `TagToolboxEditor` | Editor | Variable tag-filter customization, colored tag pill + container chips, enhanced dropdown picker, Tag Browser with References, Tag Audit, tagged graph comments. |

## Documentation

- [Designer Guide](docs/designer-guide.md) — every feature, from a user's chair.
- [Architecture](docs/architecture.md) — modules, verified engine seams, design rules, cross-version gates.
- [Backlog](backlog/tasks/) — the roadmap, in Backlog.md format.

## License

[MIT](LICENSE).
