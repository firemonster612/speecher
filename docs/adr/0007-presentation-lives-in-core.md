# Presentation lives in core

Status: accepted, 2026-09-29

## Context

ADR 0001 gave each platform its own front end over one settings schema. The
schema kept the settings rows in step, but everything outside it drifted. An
audit at ca129f6d found about 120 differences between Linux, macOS and
Windows. Most of them came from one cause: each front end wrote its own
wording for the same thing. Setup steps, tray and status labels, the update
banner, Transcribe labels, collection editors and Home's tips were each
written two to four times, in different case, spelling and terms.

## Decision

**Presentation lives in core. Front ends map core values to native widgets
and never write their own wording.** A string shown on more than one platform
belongs in core. So do the decisions behind it: which state a label names,
which action a button takes, when a note appears.

Core presentation modules:

| Module | Owns |
| --- | --- |
| `src/core/settings/SettingsSchema.*` | Pages, panes, sidebar groups, rows, options, help and disabled notes, collection chrome |
| `src/dictation/DictationTypes.*` | Dictation status, toggle captions, tray headings |
| `src/dictation/PopupPresentation.*` | Popup error wrapping, countdown, receipt outcome, preview trimming |
| `src/app/UpdateBanner.*` | Update banner and popup chip text and actions |
| `src/transcribe/TranscribePresentation.*` | Transcribe labels, steps, summaries and file types |
| `src/core/Insights*` | Home's stat captions, comparisons and share output |
| `src/app/SetupSteps.*` (#163) | Setup step titles, gates and blocked reasons |

A new module that words something for the user follows the same naming
(`*Presentation.*`, `SetupSteps.*`) or lives in `src/core/settings`, so the
checks below find it without a file list.

A front end may:

- choose the native widget, layout, icon and symbol for a core value;
- map a core icon id or outcome kind to its own icon set;
- write platform-only strings core has no reason to know, such as a
  platform-specific system dialog or accessibility name, when no other
  platform shows the same thing.

A front end may not:

- write a caption, label, help text or message that another platform also
  shows;
- restate a core string as a literal instead of reading it from core;
- decide state wording itself, such as which caption a button shows in which
  state.

House wording, for core strings on every platform: sentence case for
captions, headers and buttons, with CONTEXT.md glossary terms keeping their
capitals; US spelling; "…" (U+2026), never "...", and only when more input
follows; CONTEXT.md terms, never their Avoid terms.

## Enforcement

Two ctests, both in `tests/ui_wording.py`, run with every build:

- `speecher_ui_wording_lint` reads the user-visible literals in the core
  presentation modules and fails on "...", British spellings, Title Case
  captions and CONTEXT.md Avoid terms. The glossary and the Avoid list are
  read from CONTEXT.md itself.
- `speecher_ui_wording_front_ends` fails when a file under `src/frontend/qt`,
  `src/frontend/mac`, `src/frontend/win` or `src/ui` contains a literal equal
  to a core string of two or more words.

Both are regex heuristics, not parsers. A real exception carries
`// ui-lint: allow <rule>` on its line or the line above, with the reason.
`tests/ui_wording_allowlist.txt` lists known drift with what clears it; the
list only shrinks.

The `insights-evidence` workflow grabs the same page ids on Linux, macOS and
Windows for any pull request touching `src/ui`, `src/frontend` or
`src/core/settings`, as one `pages` artifact named `<platform>-<page>.png`.

## Consequences

- Wording changes once, in core, and every platform shows it.
- A front end that copies a core string fails the build, so drift is caught
  when it is written, not in the next audit.
- The checks catch copies of core strings, not every inline string. A new
  string written only in a front end still needs review to move it to core.
- The lint is a heuristic and will sometimes flag a real name or miss a
  caption; the allow marker and CONTEXT.md glossary are the escape hatches.
