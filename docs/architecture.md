# Tag Toolbox — Architecture

Contributor-facing map: modules, files, the verified engine seams everything is built on, and the design rules that keep the plugin safe to extend. Engine facts below were verified against installed UE source (5.8, with 5.0 spot checks) — not guessed.

## Modules

| Module | Type | Purpose |
|--------|------|---------|
| `TagToolbox` | Runtime | `UTagToolboxSettings` only — the styles registry and plugin options. No gameplay behavior; nothing ships in a cooked game. |
| `TagToolboxEditor` | Editor | All features: customizations, picker, Browser, Audit, comment tint. Everything lives in `Private/`. |

## File map (TagToolboxEditor/Private)

| File | Purpose |
|------|---------|
| `TagToolboxEditorModule.*` | Registration wiring: property customizations (deferred, see below), variable customizations, comment-tint factory, nomad tabs, console seams `TagToolbox.OpenTagBrowser` / `OpenTagAudit`. |
| `TagToolboxTagPillCustomization.*` | Editor-wide `GameplayTag` property replacement: colored pill header; dropdown = `STagToolboxTagPicker` in selection mode; commit mirrors the engine picker exactly (`FScopedTransaction` + `SetValueFromFormattedString("(TagName=\"X\")")`, `"None"` clears). `CustomizeChildren` = the shared roots row. |
| `TagToolboxTagContainerPillCustomization.*` | Editor-wide `GameplayTagContainer` replacement: the chip strip IS the dropdown trigger (inner ✕/toggle buttons consume their own clicks); collapse past `MaxVisibleTagChips`; per-chip removal mirrors the engine combo (`SetPerObjectValues` per object); all strip rebuilds triggered from a chip's own handler go through the one-tick deferral (`RequestRebuildChipStrip`). |
| `TagToolboxTagRootsChildRow.*` | The shared "Gameplay Tag Roots" child row (pill + container): edits the variable's `Categories` metadata via `FBlueprintEditorUtils::SetBlueprintVariableMetaData`, using `Property->GetOwner<UFunction>()` as the local-variable scope — byte-for-byte the engine 5.7+ recipe, which vanishes whenever a plugin owns the type customization. |
| `TagToolboxVariableFilterCustomization.*` | The "Tag Filter" row in the Blueprint variable Details (member + local channels), covering container variables (arrays/sets/maps of tags) that the native row never reaches. Registered for `FProperty::StaticClass()`; filters itself via `IsTagFlavoredVariable`. |
| `STagToolboxTagPicker.*` | The from-scratch tag tree (browse mode = Tag Browser tab; selection mode = property dropdown): search, favorites, recents (per-user per-project ini, never manually flushed), colored rows, Categories filtering via `GetFilteredGameplayRootTags`, and the browse-mode References pane. |
| `TagToolboxAudit.*` + `STagToolboxAuditPanel.*` | The audit engine (pure classification helpers + the registry walk) and its explicit-run tab. |
| `TagToolboxCommentTint.*` | `#tag` comment tinting: a visual-node-factory STAMP that writes `CommentColor` in memory at widget build and returns null so the stock widget renders. Pure token parser is worldless-tested. |
| `TagToolboxColorBridge.*` | The one color-resolve seam: own registry (exact → ancestor) first, then Paper2DPlus's `TagColors` read reflectively (no module dependency). Own registry wins outright. |
| `STagToolboxMenuHostedPickerGuard.h` | RMB swallow (preview-down AND up) for pickers hosted in auto-dismissing menus — without it the engine picker's row context menu fatal-asserts ("Window Creation Failed (1400)"). |
| `Tests/TagToolboxEditorTest.cpp` | Worldless suite (`Automation RunTests TagToolbox`): style fall-up, audit classification, near-duplicate distance, tag-flavored-variable detection, comment token parsing. |

## Verified engine seams

- **Property-type registration is silently last-wins.** Identifier-less `RegisterCustomPropertyTypeLayout` overwrites the type's single BaseCallback with no error, and `Unregister` clears rather than restores. GameplayTagsEditor registers its own customization *inside the OnPostEngineInit broadcast*, so ours registers **one tick later** (`GetOnPostEngineInit` → `SetTimerForNextTick`); hot-reload/late-load paths register immediately. The pill yields to Paper2DPlus (reflective probe of `bColorizeGameplayTagPickers`); containers have no contender.
- **Blueprint variable metadata flows to compiled properties.** `FBPVariableDescription::MetaDataArray` is copied onto both member and function-local `FProperty`s by the kismet compiler, and `SetBlueprintVariableMetaData` additionally stamps the skeleton + generated classes immediately. The tag picker resolves `Categories` through `UGameplayTagsManager::GetCategoriesMetaFromPropertyHandle`, which walks parent handles, array inner / map key properties, and owner-function metadata — never read the bare `GetMetaData("Categories")`; it silently unfilters inherited scopes.
- **Saved tags are Asset Registry searchable names.** `FGameplayTag`/`FGameplayTagContainer` serialization calls `MarkSearchableName` on save; `IAssetRegistry::GetReferencers(FAssetIdentifier(FGameplayTag::StaticStruct(), TagName), …, EDependencyCategory::SearchableName)` answers "who uses this tag" instantly with zero loads. Exact names only — hierarchy queries must expand child names themselves. Editor-only data; cooked registries filter it.
- **The engine picker's rows are sealed.** `SGameplayTagPicker`'s row generation, row style, and text color are private with no hooks — colored rows/favorites REQUIRE a from-scratch tree (this repo's `STagToolboxTagPicker`). Its public `Filter`/`PropertyHandle` args remain useful for hosting it as the container multi-select editor.
- **Comment widgets can't be recolored by subclassing.** `SGraphNodeComment` binds its body/title colors to non-virtual methods and `TitleBar` is private — hence the factory-stamp approach (which also renders for users without the plugin once assets are saved).
- **Engine rename never fixes assets.** `RenameTagInINI` recursively renames the dictionary and appends a redirect, but performs no referencer check and no resave — the fix-up gap TASK-1's headline candidate fills.

## Design rules

1. **Presentation, never authority.** Styles, filters, tints, and audits change what designers see and pick — never what tags mean, and never at runtime in a cooked game.
2. **Registry is an override list.** Empty by default; every consumer keeps a stock fallback. Never seed `FGameplayTag` rows in a CDO constructor (tag-manager init order). `TArray` of entries, never a tag-keyed `TMap` in config (UE-230676).
3. **Commits mirror the engine exactly.** Property writes use the same transaction + formatted-string / per-object-values shapes as the engine widgets, so undo and multi-edit behave identically.
4. **Never name a local or parameter `Tag` inside an `SWidget` subclass** — it shadows `SWidget::Tag` and fails as C4458 under warnings-as-errors (this bit twice on day one; use `InTag`/`NodeTag`).
5. **Widgets triggered from their own click handlers defer their rebuilds** one tick (`RegisterActiveTimer`) — a synchronous `ClearChildren` destroys the widget whose handler is still on the stack.
6. **Menu-hosted pickers are selection-only** and wrapped in the RMB guard; never push a context menu from an auto-dismissing menu host.
7. **Config writes never flush manually** on interactive paths (`GConfig->Set*` alone is durable; a manual flush rewrites the whole ini).
8. **The audit and References pane never load assets**, and their blind spots (C++ call sites, string-typed tags, unsaved edits) stay documented rather than papered over.

## Cross-version gates (for TASK-2)

| Boundary | Applies |
|----------|---------|
| `SGameplayTagPicker` / `SGameplayTagCombo` / `SGameplayTagContainerCombo` public | 5.3+ (below: `IGameplayTagsEditorModule::MakeGameplayTag(Container)Widget`, signature-stable 5.0–5.8) |
| `FCoreDelegates::GetOnPostEngineInit()` spelling | 5.8+ (gate add AND remove sites) |
| `RegisterVariableCustomization` returns a handle | 5.1+ (5.0: void return, no-handle unregister, per-class TMap clobber) |
| Native "Gameplay Tag Roots" row | 5.7+ only (our reproduction is the sole route below) |
| `FGameplayTag::GetTagLeafName` | 5.6+ |
| `FGameplayTagNode` public DevComment/source accessors | not on 5.0 (use `GetTagEditorData`, whose signature also drifts) |
