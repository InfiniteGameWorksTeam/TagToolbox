# Tag Toolbox Changelog

## v0.2.1 — 2026-08-21

### Fixed
- **Undo/redo for variable tag filters.** Setting or clearing a Tag Filter / Gameplay Tag Roots on a Blueprint variable is now a real undo step (Ctrl+Z / Ctrl+Y). The engine's `SetBlueprintVariableMetaData` records nothing on its own — which is also why the stock 5.7+ Roots row was never undoable — so the plugin now transacts the Blueprint (and the function-entry node for local variables) and regenerates the skeleton after undo/redo so the picker's filter visibly reverts, not just the stored metadata.
- **Undo/redo for tag colors.** Set Color (one undo step per slider drag, not one per mouse move) and Clear Color are transactional; undo/redo re-persists the default config and re-notifies every pill, so the editor, the ini, and the chips never disagree.
- Operations that rewrite the tag inis — create, rename, delete, redirects, and resave — remain deliberately outside the undo buffer (the engine's tag table has no transactional form); each keeps its own confirmation, preview, and rollback path instead.

## v0.2.0 — 2026-08-21

### Fixed
- Tag Audit now aggregates `GameplayTagRedirects` across EVERY tag source list (`Config/Tags/*.ini` included), not just `DefaultGameplayTags.ini`. The engine writes rename redirects to the renamed tag's own source list, so the audit previously misclassified those old names as "referenced but undefined".

### Added
- **Rename with asset fix-up**: rename a tag or whole subtree from the Tag Browser's row menu — live preview of every referencing package (with loaded/dirty/read-only status), merge detection with an identities-conflated warning, cycle and unwritable-source refusals, redirect-chain collapse, style/favorite/recent fixup, consented referencer resave, a live re-query, and an optional redirect-retirement offer. Rollback (only reachable before any package saves) restores the tag inis and detects when a restart is required. The engine's own rename writes a redirect and fixes nothing (open issue UE-194640).
- **Audit actions**: the audit list is multi-select; Delete Unused (confirmed, per-tag deleted-vs-refused summary), Create Redirect… (single-row, validated target), and Resave Referencers (shared consent dialog) turn findings into fixes. A stale banner arms on any tag-table change and clears only on re-run; lingering-redirect rows offer "Resume rename fix-up…".
- **Inline create-tag row in the picker**: type a missing tag and create it in place (parity the plugin picker lost against the engine dropdown) — filtered pickers offer a filter-root-prefixed name, the engine's fixed-string suggestion is adopted into the offer, invalid input explains itself instead of vanishing, and a tag hidden by the favorites lens offers one-click reveal.
- **Pill copy/paste**: Copy/Paste on the tag pill's row actions and right-click menu (plain names and engine export text; redirected old names resolve to their target). Refusals name the actual cause — container-shaped clipboard, unparseable text, or a tag outside the property's filter.
- **Usage counts in the Tag Browser**: an explicit "Count usage" scan puts an exact-name count badge on every row — three honest states (count / not-scanned / stale) — plus a sort-by-usage lens that orders siblings by subtree-aggregate so a hot leaf lifts its quiet parents.
- **CI tag audit**: `-run=TagToolboxTagAudit` plus `scripts/run-tagtoolbox-tag-audit.ps1` — a schema-versioned JSON report whose verdict the wrapper reads with fail-closed freshness checks (exit 0 clean / 1 findings / 2 infrastructure). Fails builds on referenced-but-undefined tags by default; switches escalate the other categories; engine/plugin content is excluded from referencer scope unless widened.
- Shared resave/preview engine behind rename fix-up and the audit's resave action: one dirty-consent rule (opted-in unsaved edits are saved first, then reloaded, then resaved), per-package outcomes attributed through the package-saved event so a mid-batch cancel reports unattempted — never silently lost.
- Shared tag scan service: one session-cached Asset-Registry usage scan (explicit-run-only, honestly-stale on tag or content changes) behind the audit — and the foundation for Browser usage counts, rename fix-up, and the CI commandlet.
- Destructive-flow engine characterization recorded in `docs/architecture.md` (rename/delete/redirect/reload/rollback semantics, verified against UE 5.8 source plus a live probe run).

## v0.1.0 — 2026-08-20

### Added
- MIT license; standalone repository documentation (designer guide, architecture) and an in-repo Backlog.md-format backlog, decoupling the plugin from its development host.
- Tag Browser References pane: selecting a tag lists every asset whose saved data references it (Asset Registry searchable-name metadata — instant, no loads), with an "include child tags" toggle and double-click-to-open. Registry granularity is the asset, not the node inside it.
- Tag CONTAINER properties get a colored chip strip that collapses past `MaxVisibleTagChips` tags ("+N more" / "Show less") with per-chip remove; the whole strip is the dropdown trigger — click any chip (or the empty row) to open the engine multi-select picker, with the remove/toggle buttons consuming their own clicks. The Gameplay Tag Roots row appears for Blueprint-created container variables too.
- Tagged graph comments (`bColorizeTaggedGraphComments`, default on): a comment box whose text carries a `#Some.Tag` token naming a registered tag is tinted with that tag's registry color when the graph builds its widgets — comment groups recolor from one place, nothing is dirtied, and stamped colors render for teammates without the plugin.
- Tag properties now open the Tag Toolbox picker as their dropdown (when the pill customization is active): colored rows, favorites, one-click recents chips, search, and full `Categories` filter honoring. The engine picker's rows are private, so the enhanced experience lives in the property dropdown rather than inside the stock widget.
- `Gameplay Tag Roots` child row on Blueprint-created tag values (parity with UE 5.7+ native, which disappears when any plugin owns the tag customization).
- Paper2DPlus Tag Colors fallback (`bUsePaper2DPlusColorsAsFallback`, default on): tags without a Tag Toolbox style resolve through Paper2DPlus's registry — read reflectively, no dependency — so a project switching chip ownership keeps every authored color.
- Console seams `TagToolbox.OpenTagBrowser` / `TagToolbox.OpenTagAudit` for scripted verification.
- Plugin skeleton: `TagToolbox` (Runtime, settings only) + `TagToolboxEditor` modules.
- `UTagToolboxSettings` tag styles registry: tag → color entries with ancestor fall-up resolve (`ResolveTagColor`), `SetTagColor`/`ClearTagColor` persistence, and an `OnTagStylesChanged` delegate. Stored as an array (not a tag-keyed map) to avoid config corruption when the tag table changes (UE-230676).
- Editor integration toggles: `bColorizeGameplayTagPickers`, `bEnableVariableTagFilters`.
