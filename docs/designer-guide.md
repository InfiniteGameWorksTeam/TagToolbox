# Tag Toolbox — Designer Guide

Everything Tag Toolbox adds lives in surfaces you already use. Nothing here changes what tags *mean* — presentation and picker guidance only, and nothing runs in a cooked game.

## Tag colors

Author project-wide tag colors in **Project Settings → Plugins → Tag Toolbox → Tag Styles**, or right-click a tag in the **Tag Browser** and choose **Set Color…**. Colors resolve with **ancestor fall-up**: style `Combat` once and every `Combat.*` tag inherits it until a child gets its own entry. An unstyled tag stays neutral gray — installing the plugin changes nothing until you author a style.

Colors drive: the tag property pill, dropdown picker rows, Tag Browser swatches, recents chips, container chips, and tagged graph comments.

If Paper2DPlus is installed, tags without a Tag Toolbox style fall back to Paper2DPlus's Tag Colors registry (`Use Paper2DPlus Colors As Fallback`, on by default). Tag Toolbox's own registry always wins.

## The tag property pill and dropdown

`GameplayTag` properties render as a colored pill. Click it to open the Tag Toolbox picker:

- **Search**, colored rows, and a **Favorites** lens (star any row).
- **Recent:** chips — your last-used tags, one click to assign. Recents outside the property's filter are hidden.
- **Clear** empties the value.
- The picker honors the property's `Categories` filter, including filters inherited from parent properties and function parameters.

## Tag container properties

`GameplayTagContainer` properties render as a wrap strip of colored chips:

- **Click any chip (or the empty row)** to open the multi-select picker — the strip itself is the button.
- Each chip's **✕** removes that tag (applies to every selected object).
- Past **Max Visible Tag Chips** (settings, default 5; 0 = never collapse) the strip collapses to "+N more"; click to expand, "Show less" to fold back.

## Filtering Blueprint variables ("only tags under X")

Select a tag or tag-container variable in **My Blueprint** and set the **Tag Filter** row in the Variable category — pick one or more root tags and every picker for that variable is filtered to those subtrees. This writes the same `Categories` metadata C++ uses. It also works for **arrays/sets/maps** of tags, which the engine's own 5.7+ row never covers.

The same filter is editable at the value site: expand the tag value row and use **Gameplay Tag Roots**.

Filters guide the picker only — Blueprint logic can still assign any tag. Pair with validation when correctness matters.

## Tag Browser (Tools → Tag Browser)

The whole tag tree with colors, search, favorites, and recents. Right-click a row for **Copy Tag Name**, **Set Color… / Clear Color**, and favorites. The bottom **References** pane lists every asset whose *saved* data uses the selected tag — instant, no loading — with an **Include child tags** toggle and double-click-to-open. It points at the asset, not the node inside it.

Console: `TagToolbox.OpenTagBrowser`.

## Tag Audit (Tools → Tag Audit)

Press **Run Audit** (it never runs on its own) for a no-asset-load report:

| Category | Meaning |
|----------|---------|
| Unused | Defined but never referenced by saved content or config |
| Undefined | Referenced by saved content but no longer defined (and not redirected) |
| Near Duplicate | Sibling tags one typo apart |
| Broken Redirect | A `GameplayTagRedirects` entry pointing at an undefined tag |
| Lingering Redirect | An old name still referenced — the listed packages need a resave to retire the redirect |

Select a row to see the referencing packages. Known blind spots: C++ `RequestGameplayTag` call sites, tags stored as plain strings, and unsaved edits — the registry only knows saved data.

Console: `TagToolbox.OpenTagAudit`.

## Tagged graph comments

Put a `#Some.Tag` token anywhere in a Blueprint comment box's text. If it names a registered tag with a registry color, the comment tints to that color when the graph opens — comment groups across the whole project recolor from one place. Notes: the tint applies when a graph builds its widgets (reopen after changing a color), it never dirties the asset by itself, and once the asset is saved the color renders even for people without the plugin. Toggle: `Colorize Tagged Graph Comments`.

## Settings reference (Project Settings → Plugins → Tag Toolbox)

| Setting | Default | Effect |
|---------|---------|--------|
| Tag Styles | empty | The color registry (ancestor fall-up) |
| Colorize Gameplay Tag Pickers | on | Own the editor-wide tag/container property widgets |
| Enable Variable Tag Filters | on | The Tag Filter row on Blueprint variables |
| Use Paper2DPlus Colors As Fallback | on | Bridge Paper2DPlus's Tag Colors when a tag has no style here |
| Max Visible Tag Chips | 5 | Container chip collapse threshold (0 = never) |
| Colorize Tagged Graph Comments | on | The `#tag` comment tint |
