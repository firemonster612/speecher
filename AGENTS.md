## Agent skills

### Issue tracker

Issues and PRDs are tracked in this repository's GitHub Issues. See `docs/agents/issue-tracker.md`.

### Triage labels

Triage uses the five default labels. See `docs/agents/triage-labels.md`.

### Domain docs

Domain documentation uses the single-context layout. See `docs/agents/domain.md`.

## UI changes: scope and rules

These rules cover the desktop app under `src/`. The Android client under
`android/` follows `android/AGENTS.md` instead.

These exist because a "make the settings rows nicer" request once turned into
a rewrite of the sidebar, the colours and the Dictation page. Do not repeat it.

- **Change only what was asked.** A request about settings rows touches
  `src/ui/settings/SettingsPageSupport.*` (row and card widgets) and
  `src/frontend/qt/SchemaSettingsPage.cpp` (how schema rows are rendered).
  The sidebar and header live in `src/ui/AppWindow.cpp`; the Home page in
  `src/ui/HomePage.cpp`; page content in `src/core/settings/SettingsSchema.cpp`.
  Do not edit a file outside the request's area without saying so first.
- **The style draws, we do not.** No stylesheets, no hand-painted frames,
  highlights, hover states or shadows, no colours other than palette roles, no
  pixel constants beyond the spacing helpers in `SettingsPageSupport.h`. If a
  widget style draws something square, that is the style's decision; the fix
  is a different native widget or a newer style, never a `paintEvent`. The one
  exception is the settings card container, which mirrors Kirigami Addons'
  FormCard (a rounded rectangle in the Base colour with a frame at
  Kirigami's frame contrast) because Qt Widgets has no such container; it uses
  palette colours only and lives in `SettingsPageSupport.cpp`. Hover on button
  rows comes from the style's own item primitive, not from us. Home's activity
  heatmap and hour bar chart are the other exception, because Qt Widgets has
  no chart widgets; they paint rounded cells and bars from Highlight mixed over
  Base and live in `src/ui/InsightsCharts.cpp`. Home's progress bars use the
  same tint: every bar but the leading one gets a palette whose Highlight and
  Accent are that mix (`makeBar` in `src/ui/HomePage.cpp`).
- **Settings pages follow Kirigami Addons FormCard.** Bold header above the
  card, title and small grey description on the left, control on the right,
  inset separators, button rows with a trailing arrow, cards capped at 30
  grid units and centred. See https://develop.kde.org/docs/getting-started/kirigami/addons-delegates/.
- **Native widgets first.** Group boxes for groups, list views for lists,
  form rows for settings. Prefer the widget the toolkit already has over a
  custom-looking replacement.
- **Look before you claim.** Any UI change must be checked in screenshots you
  looked at yourself: `SPEECHER_GRAB_PAGE=<page> speecher --grab out.png` with
  `QT_QPA_PLATFORM=offscreen`, `QT_QPA_PLATFORMTHEME=kde` and a Breeze kdeglobals
  in an isolated `XDG_CONFIG_HOME`, or the Plasma rig described in the memory
  notes. A test passing is not evidence that a page looks right.
- **Keep the page set stable.** Adding, removing or moving a page is a product
  decision the user makes, not a side effect of a layout change.
