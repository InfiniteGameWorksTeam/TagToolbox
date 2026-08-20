# Tag Toolbox Changelog

## Unreleased — targeting v0.1.0

### Added
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
