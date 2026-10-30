# Speecher Quick front end

A Qt Quick (QML) front end for every platform, replacing the Qt Widgets, SwiftUI
and WinUI front ends. The daemon shows it in its own process
(`docs/adr/0009-one-quick-ui-inside-the-daemon.md`) on Linux, macOS and
Windows. It reads and writes the real settings, and every area runs on core's
data.

## Layout

| Path | What lives there |
|---|---|
| `qml/Speecher/Theme/Theme.qml` | Every colour, size, font and motion value |
| `qml/Speecher/Controls/` | The design system: buttons, switches, rows, cards, pages |
| `qml/Speecher/App/` | The window, sidebar, search palette and pages; `ThemeSetting`, which makes Theme follow the Appearance settings, and `ColorSchemes`, the colour schemes the Colors setting offers |
| `qml/Speecher/App/pages/` | One file per page, local parts in `pages/<area>/` |
| `qml/Speecher/Popup/` | The dictation popup |
| `qml/Speecher/Wording/` | The UI's wording: `Schema` (pages and settings rows, from `schema/*.js`) and `Words` (shared words and the shell's) |
| `qml/Speecher/Demo/` | `DemoVoice`: a made-up voice level for illustrations |
| `qml/Speecher/Backend/` | `Library.qml`, the one QML type of `Speecher.Backend`, which words the C++ `LibraryLists` |
| `host/` | The app's side (C++): `QuickFrontEnd`, which loads the windows in the daemon, `QuickPopupWindow`, which carries the popup in `PopupWindowBase`, and the real `Settings`, `Dictation`, `DictationState`, `Navigation`, `Updates`, `Transcription`, `System`, `Phone`, `Removal`, `Models`, `Insights`, `Accounts`, `Audio`, `Appearance` and `LibraryLists` |
| `assets/` | Atkinson Hyperlegible Next, the Lucide icon font (company marks are path data in `qml/Speecher/Controls/brands.js`), and the circle-flags language flags `Flag` draws |

`cmake/QuickUi.cmake` builds each directory under `qml/` (the Backend with
`host/`'s C++) into a QML module compiled into the binary; each directory's
qmldir names its singletons.

## Wiring plan

Pages import `Speecher.Backend` and read only its singletons for data. The
Backend is the C++ types in `host/` plus `Library.qml`.

- Wired: `Settings` (`host/QuickSettings`),
  `Dictation` and `DictationState` (`host/QuickDictation`), `Updates`
  (`host/QuickUpdates`), `Transcription` (`host/QuickTranscription`),
  `System` (`host/QuickSystem`), `Phone` (`host/QuickPhone`), `Removal`
  (`host/QuickRemoval`), `Models` (`host/QuickModels`), `Insights`
  (`host/QuickInsights`), `Accounts` (`host/QuickAccounts`), `Audio`
  (`host/QuickAudio`), `Appearance` (`host/QuickAppearance`) and `Library`
  (`Library.qml` over `host/QuickLibrary`). No placeholders are left.
- `Appearance` is what the desktop offers the look: `systemColors`, the
  desktop's colour scheme as roles `{ window, text, accent, negative }` (the
  platform's `DesktopColors`, on Plasma read from kdeglobals and followed as
  it changes; elsewhere only the platform's accent), `installedSchemes` (`{ id, name, colors }`, KDE's
  `.colors` files in the XDG data dirs, read by core's `ColorSchemes`),
  `canBlur`, and `setBlurBehind(window, area, radius, dark)`, which asks the
  platform's `WindowBackdrop` (Wayland's `ext_background_effect`, or KWin's
  older `org_kde_kwin_blur`; macOS's `NSVisualEffectView`; Windows 11's Mica)
  to blur behind a window.
- `System.checks` are `{id, state, reason, fix, failures}`; `Health`
  (health.js) words each by id and reason, and a failed fix by its steps.
  Bindings look a check up with `Health.find(System.checks, id)`, so they
  follow the checks. `System.fix(id)` runs the fix (`fixFromSetup` in the
  setup assistant). The virtual keyboard turns on only after its typing test
  (`VirtualKeyboardTest`) puts core's test text in the field.
  `System.pasteRoute` is the steps core's delivery takes (insert, press,
  copy), `pasteNeed` what they need of the computer and `wordsStayCopied`
  whether that is missing, so words only reach the clipboard. `Phone`'s code lives from `Phone.start()` to `Phone.stop()`,
  and `QrCode` draws it through the host's `image://qr` provider.
  `Removal.start(keepSettings)` reports in `Removal.report` by part and
  outcome, and `RemovalReport` words it: under General's row, and alone in
  Main once the settings are deleted. The paste rules are Settings rows: `globalPasteRule`,
  `categoryPasteRule_<type>` (`inherit` is no rule of its own) and
  `applicationPasteRules`, a list of `{app, method, enabled}`.
- `Models` is what core knows about the providers and running models here,
  as data: speech services and refinement providers with a `state` and
  core's `problem` sentence, their ratings as `{ measure, value }` bars, the
  Local Model catalog with fits, speeds, downloads and Speed Tests, the
  hardware, the Local Runners and the Ollama pull, both Custom Endpoints'
  checks and setup's fallback offers. Each speech service and Local Model
  carries the language codes it listens for, from core's lists
  (`SpokenLanguages`, the catalog). `QuickModels.h` documents each field.
  `ModelText` (in Speecher.App) words all of it and looks entries up by id,
  so pages read `ModelText.service(id)` or `ModelText.catalogModel(id)`
  rather than Models' lists. A page that shows hardware calls
  `Models.checkComputer()` when it opens, which probes the hardware the
  first time; the daemon never loads the speech engine's backends for that
  on its own. The Local Runner rows look for runners when they load, and
  setup's Transcription step asks `Models.checkSignIns()` for the speech
  sign-ins.
- `Insights` carries core's summary for Home's period and the history
  General clears; `pages/home/words.js` words it.
- `Accounts` shows the daemon's `Accounts` (`app/Accounts`), which runs
  each service's sign-in check on a worker and queues secret writes; a
  secret row is write-only: `Accounts.saveSecret` and `clearSecret` go
  through the daemon's `SecretStore`, and QML sees only whether a key is
  saved and why a save or clear failed. Each service's `inUse`, core's
  `signInInUse`, says whether the settings sign in through it. The speech
  providers' sign-ins (ChatGPT, Claude) have one owner, core's
  `ProviderAvailability`:
  `Models` shows them, setup checks them, and renewing a sign-in on Accounts
  checks them again.
- `Settings.values[id]` and `Settings.set(id, value)` are the settings store,
  keyed by the schema's row ids. One table in `QuickSettings.cpp` maps each id
  to its `AppSettings` field, and `set` saves through the daemon's
  `SettingsStore`, so the CLI and the daemon see the same values. Every
  schema row with a value has its entry in the table; a row only some
  computers have names a `System` capability in `needs`, as `launchAtLogin`
  does (`System.canLaunchAtLogin`). The
  shortcut rows go to the controller, which binds and keeps them, and show
  the keys the controller displays. While one records,
  `Settings.startRecordingShortcut(id)` and `stopRecordingShortcut(id)` make
  the bound shortcuts let go of their keys, and `Settings.lastKeyAlone()`
  names the physical key just pressed or let go (Right Alt, not Alt), which
  a modifier let go on its own records. `Settings.takesKeyAlone(id, keys)`
  refuses Shift and Left Ctrl on their own, which typing presses all the
  time: the row says why in `errors` and keeps listening. Where the desktop picks the keys
  (`Settings.desktopPicksKeys(id)`, the portal), the row records nothing and
  `Settings.act(id)` opens the desktop's chooser.
- `Settings.errors[id]` says why a row's last save was refused, such as a
  shortcut another app owns. SchemaRow shows it in place of its help; a page
  that gives a shortcut row its own help sets `note`, not `help`.
- `Library` is `Library.qml` over `LibraryLists` (`host/QuickLibrary`): the
  Writing Profiles, custom tones
  and cleanup levels, vocabulary terms, Learned Corrections, replacements and
  app matches, read from and saved through the daemon's `SettingsStore`.
  `add(list, item)` and `update(list, key, patch)` (a key is the entry's id,
  term, phrase or app) return why core refused the entry (`blank`, `taken`,
  `incomplete`, `missing`, `builtIn`), empty when saved, and
  `Library.refusalText(list, reason)` words it for the form. `remove(list,
  key)` returns a token for `undo(token)`, or -1 when nothing went. Imports
  take the file the page's FileDialog chose. Library.qml words the lists.
- `Settings.act(id)` runs an action row (`runSetup`), and
  `Settings.completeSetup()` records that the setup assistant finished.
- `Navigation` is which page Main shows (`current`) and the row a page
  lands on (`pendingRow`); `go(pageId)` and `goToRow(pageId, rowId)` ask for
  them. The host holds it and opens Main on a page through it.
- `Updates` is the updater as `QuickUpdates.h` documents it: the running
  `version`, its state
  (checking through restarting, with check and install failures apart), the
  offered version, progress, why a step failed, and what a restart waits
  for. `attention` says the update needs the person, which the popup's
  update chip and the sidebar's link follow; `later()` puts a waiting
  restart off until another version. What's New is `whatsNew` (a nightly's
  commits with their links and GitHub's comparison, and the releases
  crossed, from core's `whatsNew`), fixed for the
  run, and `whatsNewPending` until the Updates page is opened, which the
  popup's What's New chip offers and Main opens on after an update. New
  settings are the rows whose `Schema.since` comes after the version run
  before (`Updates.newSince`).
- `Audio` is the microphones the platform sees, `devices` ({ id, label,
  isDefault }), relisted as they come and go, and the microphone test on the
  saved Input device: `testState`, `level`, `heard` (input since it started),
  `testError` (core's message) and `canToggleTest` (not while dictating).
  A page holds the test while it shows it (`holdTest`, `releaseTest`,
  `testHolders`); it runs while anything holds it, starting again after a
  dictation, and moves to a newly chosen device. `Microphones`
  (Speecher.App) words them: the device choices, a saved microphone that
  has gone, and what the test says.
- `Dictation` is the session as the popup shows it: `state` (the popup's
  phase: idle, starting, listening, paused, transcribing, refining,
  delivering, delivered, review, error), `level`, `elapsed`, `words`,
  `refined`, and `receipt`, `review` and `error`, whose fields
  `QuickDictation.h` documents. A receipt and an error are core's message
  split at its " • " into a title and a detail, with the settings page that
  fixes it; the popup words that page's button. Its `*Requested` signals go
  to the controller (start, stop, cancel, follow-up) and the session (pause,
  replace); the fix opens Main on that page. An error dismisses itself after
  core's `popupErrorDismissMs` (`error.dismissMs`), with Dismiss and a
  countdown that waits while the pointer rests on the popup
  (`errorHoldRequested`). `renewing` says a sign-in is renewing. `DictationState` is the same type with
  nothing behind it, for a page's demo popup.
- `Transcription` is core's `TranscribeQueue`: the files listed for the next
  batch, its `options` (seeded from the settings, never saved), the batch as
  it started (`batch`), its progress and its `results`, whose fields
  `QuickTranscription.h` documents. Its choice lists come from their owners
  (`Models`, `Library`, the `spokenLanguage` row), and its
  wording is the page's (`TranscribeWords`). Choosing files and folders and
  exporting open the desktop's file dialogs, titled by the page; a start,
  retry or export that fails says why in `problem`.
- `Schema` is the UI's own: pages and rows with label, help, kind and
  options. Option ids are the values core stores. Pages place rows by id with
  `SchemaRow { rowId: "…" }`.
- Area singletons (`Insights`, `Audio`, `Library`, `Models`, …) hold the lists
  and state pages show. Keep their properties plain (strings, numbers, arrays
  of objects).
  Each list has one owner (speech services and refinement providers in `Models`, profiles, tones and
  levels in `Library`, microphones in `Audio`, app types in the pasting
  schema); every other place derives from it with the same ids.
- The host (`QuickFrontEnd`) opens Main and the setup assistant when the
  daemon asks, at most one of each, and hides a window when it is closed.
  It creates `ThemeSetting` before any window, so every window follows the
  Theme, Colors, Layout, Translucent sidebar and Rounded corners settings.
- The popup has its own window (`host/QuickPopupWindow`), made with the
  engine and placed by the platform's `PopupPositioner`: an overlay
  layer-shell surface anchored to the bottom of the active screen on
  Wayland, never focused or activated. It shows when the session asks
  (`popupShowRequested(generation)`), answers `popupPresented(generation)`
  after the first frame of that dictation (a later show or a hide drops the
  answer), and hides itself once `PopupWindow.empty`, faded out, so the
  popup works whether or not Main was ever opened.
- The popup window is larger than the capsule, so only the capsule (and the
  offers over it while they show) may take the pointer: the host sets the window's input region to
  `PopupWindow.inputRegion` (`QWindow::setMask`, which is the surface's
  input region on Wayland; on X11 the input shape alone, since a mask
  would clip the shadow) whenever it changes, frame by frame as the
  capsule morphs.
  `SPEECHER_POPUP_CAPTURE_DIR=<dir>` saves the popup's frames while it is up.
- Wording belongs to the UI. It comes from the schema row, `Words` (shared
  words such as Cancel, Save and Copied, and the shell's: `Words.shell`,
  `Words.results(count)`) or the page, kept short and in one place per page.
  A C++ Backend type carries data, not wording. The popup words itself
  (`DictationPopup.t`) and Home words the dictation's state; the
  messages core writes (receipts, errors, a review's summary) come through
  as they are.

## Look and motion

The look comes from a stenographer's pad, the notebook dictation was taken
in: pale green-grey paper, dark ink, and a blue pencil for marks you can act
on. Settings sit in grouped rows on raised cards, a shade lighter than the
page, so each group reads as one sheet.

Palette (all in `Theme`; never write a hex value in a page):

| Token | Light | Use |
|---|---|---|
| `paper` | `#E4E8DF` | Window and sidebar |
| `sheet` | `#EEF1EA` | The page |
| `text` | `#1D2320` | Ink: text; primary buttons are filled with it |
| `raised` | `#FBFCF9` | Raised cards that hold grouped rows, buttons, menus |
| `rule` | blue-grey, 16% | Dividers between rows |
| `accent` | `#2B54C9` | Blue pencil: switches, selection, links, added text |
| `negative` | `#D23F34` | The margin's red: live sound, failures, the popup's margin rule, and words a proof strikes out |
| `warning` | `#9A6A12` | Something to set up or fix that isn't a failure: a step still open, the chosen service not signed in (an unchosen card says its state in plain ink) |
| `positive` | `#2E7D4F` | A check that passed, such as a sign-in or a delivered dictation |

The pad is Steno, the first of the colour schemes (Colors, on Appearance). A
scheme is six roles, in light and dark: `paper`, `sheet`, `raised`, `text`,
`accent` and `negative`, and a `rule` if it wants its own. Every other
colour (hover, borders, fills, the quieter inks, ink on the accent, key
caps) derives from those in `Theme`, so a scheme picks six colours and
nothing else.

- Speecher's own schemes are `Theme.presets`: Steno, Blueprint, Lagoon,
  Lilac, Rose and Sand. Each tints its paper with its accent's hue, so the
  whole window takes the colour, not only the switches. A new preset gives
  both variants, the cards a step lighter than the page.
- A desktop scheme (System, or one installed) comes in as roles, and
  `Theme.schemeOf` maps them, in one place: as light or dark as the theme,
  its window colour is the paper and its text the ink; the other way round,
  its hue tints our own paper and ink. The page and cards are steps lighter
  than the paper. Its text must reach 7:1 on the page, the cards and the
  translucent sidebar over any desktop; where the text can go no further,
  the paper gives way.
- Every scheme's inks are pushed until they read wherever they are laid:
  text at 7:1, secondary text at 4.5:1 and tertiary at 3:1 on the page,
  cards and sidebar, the accent at 4.5:1 on the page and cards, failures,
  warnings and passes at 4.5:1 everywhere. Ink on
  the accent takes white or the accent's deep shade, whichever reads better.
  `test_quick_appearance` checks every preset and a few garish schemes,
  opaque and translucent.
- Light, Dark and System work with every scheme; `ColorSchemes.source(id)`
  is what `Theme.colorSource` takes, and a scheme that has gone is Steno.
- Thumbnails of a scheme, a theme or a layout are `MiniWindow`s in a
  `LookPicker` (Appearance's, in `pages/app`), with the pencil outline gliding to
  the chosen one.

Translucency: with Translucent sidebar on and `Appearance.canBlur`,
`Theme.translucent` makes the window's paper (`Theme.sidePaper`) let 15%
(light) or 16% (dark) of the desktop through, and `ClientWindow` asks the
compositor to blur behind its frame. The sidebar's inks are checked against
that paper over a black and a white desktop. The page and cards stay
opaque; search lays its blurred copy of the window on opaque paper. On
Windows, whose Mica fills the whole window, the window drops its drawn
shadow margin and corners for DWM's. With Rounded corners off (Appearance),
the window's corners are square, its own or DWM's. Where
the platform cannot blur (no KWin blur, X11, Reduce transparency,
Transparency effects off, high contrast) it stays opaque, and the row says
why. Never make paper translucent without the blur: text over a sharp
wallpaper does not read.

Layout (Appearance): Framed puts the page on a `Sheet` inset under the title
strip; Split runs it edge to edge beside the sidebar, divided by a rule,
with the page's content starting under the strip. `Sheet` eases between
the two over `Theme.page` (at once under reduced motion), and setup
follows the same setting.

- Type is Atkinson Hyperlegible Next, one family, through `Txt { variant;
  tone; strong }`. Headings are bold; nothing is set in capitals or
  letter-spaced. Use a variant (`labelSmall` for buttons and chips, `code`
  for commands, `transcript` for dictated words, `lead` for a large sample)
  or `strong` for emphasis rather than setting `font.*` in a page.
- Red means sound is live (the record dot, the level meter) or something
  failed. Blue pencil means you can act. Everything else is ink on paper.
- A page is its title, a line of explanation, then Sections: a heading and
  caption above a raised card of rows with inset dividers. Keep pages dense:
  use the width, put short related things side by side, no empty heroes.
- Radius follows hierarchy: the page sheet is softest, cards and controls
  are firm, rows inside a card have none.
- Icons only where they help find something: the sidebar, a file type, an
  action. No tinted icon squares, no icon on every row.
- No gradients, glows, coloured shadows or decorative washes.
  The popup's scatter ribbon is the one exception: its afterglow and soft
  glow are part of the level animation.

## Consistency rules

One thing looks and behaves one way everywhere. Fix a difference in the
shared control or Theme, not in a page.

Choosing:

- A provider or service is chosen with `ServiceCards`, in settings, setup
  and Transcribe alike: a grid of cards, each with its icon, name, a line of
  help, its status and its ratings, so they compare before choosing, and a
  check on the chosen one. A narrow place gets fewer, smaller columns, not
  another layout. A profile's own override stays a `Select` with an explicit
  Default option.
- A provider company has one name everywhere, "ChatGPT" or "Claude", with
  its mark (`Icon { name: "openai" }`, `"claude"`); how it signs in is help
  or Accounts' business, never part of the name.
- A check means a committed choice. The editor that is open (a profile in
  the list) is a selected `ListRow`, with no check.
- Two to four short options are segments; more, or a list that grows, is a
  `Select` (`CompactChoice` picks for you; SchemaRow does the same), except
  a language: the Spoken language is a `LanguageSelect`, with flags and a
  search, wherever it is chosen. `Chip`s are for choosing several at once or
  inserting a snippet.
- A count of languages a service or Local Model listens for is a link
  (`LinkedText`, or `helpLink` on ServiceCards and SettingRow) that opens
  `LanguageList`, the same popover showing only those languages. Following
  it never chooses or opens the card it sits on.
- Languages are named once, in the `spokenLanguage` row's options (English,
  then the language's own name), which cover every code core knows; lists
  of codes take those names (`ModelText.languageOptions`).
- `SettingRow` for settings, `ListRow` for data. Rows start their content
  `Theme.rowInset` in from the card and end it the same distance from the
  right.

Press, hover, selection:

- Hover and press are colour changes over `Theme.fast`: `fill` under the
  pointer, `fillStrong` while pressed (`surfaceHover`/`surfacePressed` on
  raised buttons and fields). Nothing grows under the pointer.
- Only a button or a whole-card choice shrinks when pressed: text buttons
  and chips to 0.965, icon buttons to 0.92, `ChoiceCard`s (`OptionCards`,
  `ServiceCards`) and theme thumbnails to 0.98, over `Theme.fast` springy.
  Rows, menus, segments, fields and the header of a card that opens never
  shrink.
- A card that opens is a `Card` whose header has the row wash and a chevron
  that turns over `Theme.base`; its body slides open. Opening never selects
  it. `SettingRow { clickable }`, `Disclosure` and ModelCard work this way.
- A selection glides to the new item over `Theme.slow` springy. A selected
  card keeps its 1 px border and changes its colour and check.
- A control that is disabled fades to `Theme.disabledOpacity`, easing. The
  control owns the fade; a row fades its title and icon, not its help,
  which says why it is off, and the control in it fades itself, so nothing
  fades twice. Something inactive but still
  editable stays legible and says why in its help; it is not dimmed.
- The focus ring shows for keyboard focus only: `visualFocus` on Templates
  controls, `FocusVisible` on custom ones. A text field shows its soft
  focus halo while it is being edited. A custom control that acts has Tab,
  Return and Space, and the right `Accessible.role`.
- Exceptions, named by role: a slider's handle grows while held, a dragged
  fallback row lifts to 1.02, a switch's knob stretches toward where it is
  going while held and on a flip, and can be dragged, the popup's round
  buttons never take focus. Do not copy them elsewhere.

Shape:

- Radii: `radiusSmall` 4 for small shapes, `radius` 6 for controls,
  `radiusLarge` 8 for cards, panels and menus, `radiusPanel` 12 for the
  sheet. The popup is one pill, half its bar's height round, and keeps that
  radius as it grows. A shape set concentrically inside another takes the
  outer radius less the gap: a row's wash sits `Theme.washInset` inside its
  card with `Theme.washRadius`; a menu's items are 4 inside an 8 panel; a
  segment's thumb is 3 inside a 6 track. Circles take half their size.
- Borders are 1 px: `sheetBorder` on the shell, `border` on cards and
  controls, `borderStrong` under the pointer and on floating panels, `rule`
  between rows, `accent` for a selection. Focus rings are 2 px.
- Surfaces: `Card` holds a group, `Surface` floats (menus, palettes,
  toasts), `Plate` is recessed under a picture, `Sheet` is the shell's page.
  Do not put a raised card inside a raised card; a form inside a card is a
  `fill` wash.

Layout:

- A form: `FormField` label to control 6, fields and columns 16 apart,
  actions 8 apart. Settings save as they change; an `InlineForm` that adds
  or edits an entry has Save and Cancel.
- Page commands go in `Page.headerActions` at the default 32 px size; list
  and group commands go in `Section.actions` at `size: "small"`. Adding to a
  list goes beside the list's heading and is not primary; primary is for the
  one task that commits work, such as Transcribe.
- An empty list is `EmptyState { compact: true }` on a card (a line on the
  row inset) or a centred `EmptyState` for a page's list and search results.
  List controls never draw their own.
- Deleting something a person made either asks first with `InlineConfirm`
  (a question, what it costs, Cancel and the action) or happens at once
  with an Undo in its toast. Removing one of a few chosen fallbacks is
  immediate.
- The title strip is `Theme.titleBarHeight` 52. `Brand` puts the mark 12 in
  from the window's top and left and the window buttons sit 12 in from the
  top and right, in the main window and setup alike. On macOS the buttons
  are traffic lights 20 in from the left (`TrafficLights`), and the brand
  starts after them. Framed, the sheet
  starts under the strip; split, the page's surface reaches the top and the
  strip lies over it; the 14 px shadow room around a framed window goes when it is
  maximized and is not padding.
- The sidebar is one list with no headings: seven top-level pages (Home,
  Transcribe files, Dictation, Speech to text, Refinement, Pasting,
  General), in the order a dictation goes, and their subpages. A page sits
  under another (`parent` in `schema/pages.js`) when it is a part of that
  page a person looks for on its own: Shortcuts, Microphone and Popup &
  sounds under Dictation, Local models under Speech to text, Writing
  profiles and Vocabulary under Refinement, App rules under Pasting, and
  Appearance, Accounts, Permissions & health and Updates under General.
  Top-level entries have icons; subpages have none, set in past a guide
  line from their parent's icon, with their wash and the pill on them
  starting right of the guide.
- One group is open at a time, growing in through a `Reveal`: asking for a
  page (`Navigation.requested`, the page showing included) opens the group
  holding it and closes the rest, and so does opening a group with its
  chevron. A group closed by hand stays closed when its parent is asked
  for, until it is opened again or one of its subpages is asked for. While
  a current subpage is folded away the pill sits on its parent, and a
  subpage that had keyboard focus hands it to the parent when its group
  closes. Up and Down step through the entries that show, Right and Left
  open and close a parent's subpages.
- A subpage's title leads with the path to it ("Dictation › Shortcuts"),
  each step a quiet link to that page, and search gives a subpage and its
  rows the same path. A parent lists its subpages in a `SubpageRows` card:
  at its end, or first where the page is mostly a way into them (General,
  Dictation). Dictation's rows say how each is set now (the shortcut's
  keys, the microphone, live text and sounds) in place of the subtitle. A
  subpage keeps its id, so links to it still land.

Things to avoid, because they read as generated:

- A big number with a small label and a coloured trend arrow as the default
  way to show a statistic. Say it in a sentence or a plain table unless the
  number really is the point.
- Labels above content that repeat what the content says, ALL-CAPS eyebrows,
  "A · B · C" meta strings (use commas or separate elements), "→" on links
  and buttons, a single accented word in a heading.
- A badge or pill for every state. Use plain status text; a coloured dot is
  enough for state that needs attention.
- Sparkle icons for AI. Refinement is copy editing: use pencil marks.
- Copy that sells or narrates ("Morning talker", "Your dictation, at a
  glance"). Say what something is or does.

Motion: everything that changes, moves. Nothing appears or vanishes in one
frame. The rules, after Emil Kowalski's "Great animations" and Benji
Taylor's "Family Values":

- Most motion is under 300 ms and eases out (`Theme.easeOut`). Springy
  overshoot (`Theme.springy`) is for things that land: knobs, thumbs,
  selections. Animate position, scale and opacity, not layout, except for
  height on things that open and close.
- Things fly rather than teleport. Tabs (`Switcher`) slide toward the side
  that was picked; pages slide in the direction the sidebar moved; menus and
  popovers grow from what opened them; selections glide to the new item.
- Things that appear or disappear change height and opacity together:
  `Reveal { open }` for blocks (`sideways` for a control in a row),
  `SettingRow { shown }` for rows, `RowList` for lists. Never toggle
  `visible` on something the person can see.
- A page that has just loaded is simply there: sizes snap on the first
  layout and ease after it (`Grow`), and a list's first rows do not fade
  in.
- When text or a number changes, keep what stays the same still and animate
  only what changed.
- Every animation can be interrupted: drive it from Behaviors and bound
  targets so a new change retargets it, not from one-shot sequences.
- Actions done from the keyboard many times a day (search, shortcuts) get
  the fastest motion or none.
- Delight goes to rare moments (finishing setup, a milestone, sending
  settings to the phone), not to things seen every day.
- Reduced motion keeps the fades and drops the travel: multiply every offset
  by `Theme.movement`, and pass every scale through `Theme.motionScale(s)`.

Every animation is an `Ease` (numbers) or `EaseColor` (colours), never a
bare `NumberAnimation` or `ColorAnimation`, which fall back to Qt's linear
curve: `Behavior on opacity { Ease {} }`, `Ease { duration: Theme.slow;
curve: Theme.springy }`. A height or width that opens, closes or follows
its content uses `Grow on implicitHeight {}`, which skips the first
layout. Durations come from `Theme.fast`, `Theme.base`,
`Theme.slow`, `Theme.page` or `Theme.ms(n)`, except a countdown's, which is
the real time it counts (the popup error's `error.dismissMs`); curves from
`Theme.easeOut` (the default), `Theme.easeInOut`, `Theme.springy`,
`Theme.easeIn`, `Theme.breathe` (loops that pulse) or `Theme.steady` (motion
that never settles, such as a sweep, and a countdown draining). Timer
intervals use `Theme.ms(n)` too.

The shared controls already move; use them rather than animating by hand:

| Change | Use |
|---|---|
| Tabs, or any one-of-several views | `Switcher { currentIndex }` beside a `SegmentedControl` |
| A block that appears on demand | `Reveal { open }` (and `Disclosure`, `InlineForm`, `InlineConfirm`, which open through it) |
| A control that folds away in a row | `Reveal { sideways; open }` |
| A settings row that comes and goes | `SettingRow { shown }` (SchemaRow's `hiddenWhen` does it for you) |
| Rows added, removed, reordered | `RowList { values; keyOf }`: replacing the array moves rows instead of rebuilding them |
| A number, word or note that changes in place | `MorphText` (`wrap` for text over several lines); `Section` titles and captions, `StatusText` and `Button` labels already use it |
| A mode of a proof sample | `DiffText { mode }` cross-fades and eases its height |
| Something finished out of sight | `Toaster.show(text, { icon, tone, actionText, action })` |

## Component catalogue

Every control below is in `Speecher.Controls`.

Controls never import `Speecher.Backend`: the page reads the singletons and
hands the control plain values and options, and its wording too, since
controls have no words of their own (`Words` has the shared ones). Choice
controls show `currentId` and ask for a change through their signal; the
caller sets `currentId`, from the store or from its own state. A disabled
control fades to `Theme.disabledOpacity`.


Layout and rows:

- `Page { title; subtitle; headerActions; path; pathPicked(id) }`: a scrolling page. With no title the page brings its own heading. `path` is the pages above it (`[{ id, title }]`), which lead its title as links; Main sets it from the schema and opens the page a step asks for.
- `Section { title; caption; actions; plain; cardPadding }`: a heading over a raised card. Two side by side line up with `minimumHeaderHeight`/`minimumCardHeight` set from each other's `naturalHeaderHeight`/`naturalCardHeight`. Sections stacked outside a Page (in a Reveal or a Switcher view) go in a `Column { spacing: Theme.sectionGap }`.
- `Card`: the raised card on its own. `Surface`: a floating panel (menus, palettes). `Plate`: the recessed panel a page's or setup step's picture sits on.
- `SettingRow { title; help; helpLink; iconName; stacked; clickable; trailingText; shown; first; highlight() }`: one setting with its control on the right; `helpLink` makes that part of the help a link (`helpLinkActivated(at)`).
- `SchemaRow { rowId }` (in Speecher.App): a SettingRow built from the schema.
- `ListRow { title; detail; iconName; titleVariant; titleTone; wrapTitle; detailLines; first; clickable; selected; trailing; actions }`: a data row, not a setting. `trailing` always shows (a value, a status); `actions` are buttons that show on hover or focus. Rows that need more lay out their own content in the default slot.
- `RowList { values; keyOf; entry(key); first }`: a non-scrolling list of rows that animates adds, removes and moves. Delegates take `required property string key`, read `list.entry(key)` (still there while a row fades out) and set `first: index === 0 && list.first`.
- `Disclosure { title; help; trailingText; open; highlight() }`: a row that opens to more rows.
- `Reveal { open; sideways }`: content that opens and closes in place. Rows inside draw their divider unless the Reveal starts its card, so no `first: false` is needed.
- `Switcher { currentIndex }`: one of several views, sliding between them. `width: implicitWidth` sizes it to the current view, for buttons that swap in a row.
- `FormField { label; help }`: a label and help around one input in a form.
- `InlineForm { open; title; saveText; canSave; error }`: an add or edit form in place, instead of a dialog; `error` says why a save was refused.
- `InlineConfirm { open; text; detail; confirmText; variant; leading }`: the question before an action that can't be undone: always a question and what it costs; leading (a check box) goes under them.
- `EmptyState { iconName; title; text; compact }`: what an empty list says. compact on a card, centred elsewhere.

Choosing:

- `Switch`, `CheckBox`, `Slider { format }`.
- `SegmentedControl { options; currentId; activated(id) }`: two to four short choices in a row.
- `Select { options; currentId; placeholder; chosen(id) }`: a dropdown.
- `LanguageSelect { options; currentId; shownId; placeholder; searchText; clearText; emptyText; chosen(id) }`: the current language's flag and name, opening a `LanguagePopover` to choose from. `shownId` is what the button shows when the checked option stands for another language, as a fallback's "Same as main language" does.
- `LanguagePopover { languages; currentId; choosing; searchText; clearText; emptyText; align; openFrom(item); chosen(id) }`: languages with their flags under a search field that filters by English name, own name or code; arrows, Return and Escape. `choosing` checks `currentId` and picks; without it the list only shows. It grows from what opened it, as menus do. An option's `icon` stands in for the flag (Automatic).
- `CompactChoice { options; currentId; stretch; chosen(id) }`: segments for up to four options, a Select for more.
- `OptionCards { options; currentId; activated(id) }`: a few choices as cards with help.
- `ServiceCards { options; currentId; unratedText; helpLinkActivated(id, at) }`: a provider or service as a grid of cards inside a Section's card, each with its icon, help, status, ratings and a note on them, and a check when chosen; an option's `helpLink` makes that part of its help a link. `ChoiceCard` is the card both it and OptionCards use.
- `Chip { text; iconName; checkable }`: small choices or snippet buttons in a Flow.
- `FallbackList { ids; options; statusOf; rowControl; addLabel; emptyText }`: an ordered list to drag, reorder, add to and remove from; `rowControl` puts a control on each row, such as a speech fallback's Spoken language.
- `EditableList { items; emptyTitle; emptyText; formOpen }`: things a person made, with edit and delete.

Input:

- `TextField { iconName; variant; secret; trailing }`, `SearchField`, `SuggestField { suggestions; committed }`, `TextArea { mono; minHeight }`: all draw `InputFrame`. `variant: "title"` sets a name edited where a title would be. Fields default to `Theme.fieldWidth`, and `Select`, `SuggestField` and `ShortcutField` to `Theme.selectWidth`, so choosers line up in a column of rows; leave them at that in settings rows.
- `ShortcutField { keys; emptyText; listeningText; clearText; escapeRecords; keyAlone; takesAlone; recorded(keys) }`, and `KeyCombo { keys; size; pressed; lit }` / `KeyCap { text; size }` to show keys.

Actions and menus:

- `Button { variant; size; iconName; trailingIcon; trailing; busy; tooltip }`, `IconButton { iconName; tooltip }`, `TextLink { text; iconName; variant; quiet }` for a link in a line of text; `quiet` is secondary ink rather than pencil, for a page's path in its title. `LinkedText { plainText; link; activated() }` is a Txt with one part of it a link (Tab reaches it, and a click goes no further than the link). `trailing` ends the button with an item, such as the `KeyCombo { size: "small" }` of its key.
- `CopyButton { text; tooltip; copiedText; copied() }`: icon-only without text; the caller copies on `copied`.
- `Menu { items; currentId; picked(id) }`: a menu declared inside what opens it. `MenuButton { label; items; confirm(text) }` is a Button with one. `MenuItem` (an icon or a language's `flagCode`) and `MenuPopup` are their parts, shared with Select and LanguagePopover.

Status and feedback:

- `StatusText { text; tone; busy; indicator }`: a state in words, with a dot or spinner. `tone: "live"` is sound being heard (a pulsing red dot, the text stays secondary); `tone: "tertiary"` is a state waiting its turn.
- `Notice { text; tone; indicator: dot|icon|none; inset; actionText }`: why something needs attention, and its fix.
- `Badge { text; tone; dot }`: a short outlined word, for menu entries and small labels, not for every state.
- `StatusDot { color; pulse }`, `Spinner`, `ProgressBar { value; indeterminate }`.
- `Tip { text; detail }` for a control's tooltip, `PointTip { text; detail; shown; pointX; pointY }` for a chart's, both drawing `TipBody`.
- `Toast` (one in the window) shows what `Toaster.show(...)` posts.
- `MorphText { text; wrap; maximumLineCount; elide }`: text that animates only its changed characters, or fades when it runs over several lines.

Pictures of speech:

- `Waveform { level; active; fitWidth; compress; threshold; fadeOlder }`: live input level.
- `RatingBars { bars; appearance: compact|wide }`: ratings out of ten.
- `StepNode { number; title; detail; stage }`: one stop in a run of steps; wrappers lay them out and draw the lines.
- `DiffText { segments; mode: marked|spoken|result }`: an edit marked like a proof; also `html(segments, mode)` for the same marks elsewhere.
- `AppIcon { name; iconName }`, `Icon { name }` (a Lucide name, or a company's mark from `brands.js`), `BrandMark` (Speecher's own mark), `Flag { code; size }` (a language's round flag, one per code, from the bundled circle-flags SVGs, the same on every platform).

Helpers:

- `Grow on implicitHeight {}`: a size that eases once laid out.
- `ScrollBar { persistent }`: `T.ScrollBar.vertical` on a scrolling view. It shows while scrolling; `persistent` keeps a narrow one in view, as `Select` and `Menu` lists do, and their rows end its `gutter` before it.
- `Caret { edit }`: the blinking pencil caret after a TextEdit's last character, declared inside the edit.
- `RichText.escape(s)`: text made safe to put in rich text, before adding markup.

In Speecher.App, besides SchemaRow:

- `SubpageRows { pageId; lines }`: a card of rows leading to a page's subpages, with their titles and subtitles, or a line of `lines` by subpage id, such as how it is set now, in place of the subtitle.
- `SearchLanding { rowIds; target; landed() }`: where a search for schema rows lands when no SchemaRow stands for them (a custom row, a Disclosure, a card or a section). It scrolls to target and emits landed so the target can flash or open.
- `AccessibilityNotice { text }`: the note at the top of a card whose rows need desktop accessibility.
- `HealthRow { rowId; checkId }`: a schema row showing one of `System.checks` with its fix; items inside follow the fix button. `StateText { stateId }` words a sign-in or check state, and `States` (states.js) maps states to tones: a state to sign in to, turn on or set up is `warning`, one that failed or can't be used `negative`.
- `SignInRow { serviceId }`: under the cards that choose a speech service, while it isn't ready, how to sign in (or why the last check couldn't reach it) and Check again; the card keeps the status.
- `RefinerSignInRow { providerId }`: under the cards that choose a refinement provider, while its sign-in isn't ready, how to sign in with Open Accounts, or, where Accounts couldn't read the keychain, Check again.
- `CommandChip { command }`: a command in code type with a copy button.
- `StartCommandRow`: the `startCommand` row, the command that starts one dictation with a copy button, wherever the desktop gives no shortcut.
- `Sheet { side }`: the page a window shows beside its side column, framed or split as `Theme.split` says.
- `Brand { title; detail; detailTip }`: the mark and the window's name in the title strip, for the main window and setup.
- `LanguageList { show(at, codes) }`: a LanguagePopover that only shows the languages a service or model listens for, named and worded.

## Building pages

- `Page { title; subtitle; headerActions: [...] }` holds `Section`s. Its
  title is `Schema.page(id).title` (a subpage's path comes from Main), and a Section holding schema rows takes
  `Schema.row(id).section`, so a page never retypes a title.
- `Section { title; caption; actions: [...] }` puts its title above and its
  children on a raised card; `plain: true` drops the card for content with
  its own surface. Custom rows inside a card keep the `Theme.rowInset`
  inset SettingRow and ListRow use.
- Lists that change: give the data to a `RowList` as `values`, with
  `ListRow` delegates. Don't clear and refill a ListModel, and don't toggle
  `visible` on rows or empty states; use `Reveal` or `SettingRow.shown`.
- A custom row that stands for a schema row gets a `SearchLanding`, so a
  search for it lands somewhere.
- `SchemaRow { rowId }` renders a schema row with the right control.
  `SettingRow { title; help; iconName }` with a custom control inside covers
  anything the schema can't describe.
- Add the page's rows to `qml/Speecher/Wording/schema/<area>.js`, and their
  value rows to the table in `host/QuickSettings.cpp`. Row fields
  are documented at the top of `Schema.qml`. Give rows good `keywords`; search
  depends on them.
- Templates controls (`QtQuick.Templates`) have no implicit size: set
  `implicitWidth`/`implicitHeight`. Do not import `QtQuick.Controls` (it
  brings a platform style) or Kirigami.
- Don't name an id after a built-in property (`palette`, `data`, `state`,
  `parent`, `children`, `font`).

## Checking

`ctest -R speecher_quick_pages` opens Main, the setup assistant and the popup
as the daemon does, over the real Backend, in light and dark, and visits every
page, a few searches, every setup step and every popup state. Any warning the
QML engine reports fails it; fix all of them. `ctest -R speecher_qmllint` runs
qmllint over the UI's modules, and `.qmllint.ini` makes any lint warning fail
it. A page is done when you have looked at it in both themes yourself.

With `SPEECHER_GRAB_MAIN_WINDOW=<file.png>` set, the daemon saves the real main
window a moment after each time it shows it, since a headless compositor lets
no other client take a screenshot. `packaging/verify-appimage.sh` uses it to
check that the packaged UI opens.
