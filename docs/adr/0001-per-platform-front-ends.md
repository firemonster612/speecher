# Per-platform front ends over a shared settings schema

Status: accepted, 2026-08-19

## Context

Speecher's UI is Qt Widgets, written for KDE, and the macOS port so far restyles
those widgets rather than using the platform's own controls. Restyling has a
ceiling: Qt has no `NSSwitch`, no source-list sidebar behaviour, no inset-grouped
`Form` sections, and no per-control Liquid Glass, because those are behaviours of
AppKit and SwiftUI views rather than of pixels a widget can paint.

Windows is coming and should not force a third rewrite of the same screens.

Two facts from surveying the code decide the shape:

- The layers below the UI are already portable. `src/core`, `src/dictation`,
  `src/providers` and most of `src/platform` contain no widget code, and nothing
  below `src/app` includes `src/app` or `src/ui`.
- The settings UI is mostly declarative in disguise. Of 46 settings rows, 15 are
  plain value bindings and 31 can be described with a small schema; the
  remaining 15 are bespoke, and 5 of those are the same collection-editor shape
  repeated (application rules, paste rules, vocabulary, learned corrections,
  replacements). Every parser, validator, normaliser and default already lives
  in `src/core`; the pages hold only widget mechanics.

Meanwhile `load` / `appendToDraft` / `hasChanges` is hand-written per field in
each page — roughly 140 sites that must be kept in agreement, where forgetting
the third silently breaks auto-save.

## Decision

Each platform gets its own front end, and they share a declarative description
of the settings surface rather than a widget toolkit.

**Front ends.** One per platform, each owning its own window chrome, navigation
and rendering:

| Platform | Front end |
| --- | --- |
| Linux | Qt Widgets, KDE-flavoured (the existing UI) |
| macOS | SwiftUI, with AppKit islands for the collection editors |
| Windows | WinUI 3 hosted in the Qt-owned process |

The macOS front end talks to the C++ core through one `@objc` Objective-C++
facade, so Swift never sees a C++ header and no C++ interop mode is required.
Qt keeps running the event loop on macOS — it already drives `NSApp` — and the
front end owns its own `NSWindow`s.

**Settings schema.** `src/core` gains a description of the settings surface:
pages, sections and rows, where a row carries its label, help text, control
kind, a getter and setter over `AppSettings`, an options provider for choices
sourced from a registry, an `enabledWhen` predicate for capability and cross-row
gating, and a flag marking values whose population is expensive. Rows that
resist description declare a custom identifier and are supplied by the front end.

Each front end implements one renderer over that schema plus a handful of custom
rows. Validation returns messages rather than an enum the caller re-narrates.

**Pane arrangement.** Amended 2026-09-27. The schema also says how the
settings window is arranged, and all three front ends read it: the panes (id,
title, platform-neutral icon id, layout), their order and sidebar runs, and
which schema section each pane shows (`settingsPanes()` and
`settingsSidebarRuns()` in `SettingsSchema.cpp`). A pane group's heading is its
section's title, so the two cannot differ. Pages are addressed by one id space,
a pane id or `pane:view` for one view of a pane, used for screenshot automation,
links between pages and notification targets. A window opened from hidden
shows Home, and "Settings…" opens General. Text
that names a page takes the name from `paneTitle` or `paneTitleForRow`, and the
sidebar search runs over `searchPanes`. A front end maps icon ids to its own
icons and otherwise holds no page list of its own.

**UI seam.** `ApplicationController` keeps the session, permission and IPC
responsibilities and loses its widget knowledge: window and dialog requests, the
"front end is on screen" signal that currently rides on Qt paint events, and the
alert sound become ports the front end implements. `PopupPositioner` stops taking
`QWidget *` so `speecher_platform` no longer links Qt Widgets.

## Consequences

- A new settings row is added once, in core, and appears on every platform.
- The sidebar search index comes from the schema instead of scraping the widget
  tree for label text.
- Every platform shows the same panes in the same order with the same headings,
  and adding, moving or renaming a pane is one edit in core.
- macOS gets real system controls and materials, including Liquid Glass, because
  the views are AppKit and SwiftUI rather than approximations of them.
- Windows costs a renderer, not a rewrite.
- The price is a new core layer and rewiring nine Qt pages onto it. Those pages
  keep working throughout: the renderer replaces them page by page, and any row
  that has not been described yet stays bespoke behind a custom identifier.
- Two pieces of domain logic had to move down before a second front end could be
  correct, and both have: duplicate paste-rule detection is `validatePasteRules`
  and the legacy writing-profile-override migration is
  `recognitionRulesWithMigratedProfileOverrides`, each reached through the
  descriptor that needs it rather than through the page that used to hold it.
