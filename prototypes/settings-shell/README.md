# Settings-shell layout prototype (THROWAWAY)

This is a disposable web prototype for judging **layout and interaction only**:
which category-sidebar / page-based settings structure best fits the retained
Speecher features and KDE application conventions. It is not production code,
proves nothing about final Qt/Breeze rendering, and must never be shipped or
imported from production sources.

## Run

```sh
bun run serve
```

Then open <http://localhost:4173/>. No dependencies, no lockfile.

## Variants (one route, `?variant=`)

- **A — System Settings shell**: classic KDE System Settings / KConfigDialog
  pattern. Flat category sidebar, one page at a time, form-column pages,
  Defaults/Apply footer.
- **B — Compact expert dialog**: dense single-scroll preferences dialog with a
  filter field and an anchor strip. No page switching; help lives in tooltips.
- **C — Control-center master/detail**: wider three-pane layout. Grouped
  two-line master list, detail page with intro prose, and a contextual
  explanation pane that follows the focused control.

Switch with the bottom-center switcher or Left/Right arrow keys (ignored while
a form control has focus). State is in memory only and survives switching
variants within the page session.
