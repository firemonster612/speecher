# Speecher Quick front end

A Qt Quick (QML) front end for every platform, replacing the Qt Widgets, SwiftUI
and WinUI front ends. The daemon shows it in its own process
(`docs/adr/0009-one-quick-ui-inside-the-daemon.md`); on Linux it reads and
writes the real settings, and the areas not wired yet still run on mock data.

## Layout

| Path | What lives there |
|---|---|
| `qml/Speecher/Theme/Theme.qml` | Every colour, size, font and motion value |
| `qml/Speecher/Launch/Launch.qml` | `Launch.arg(name, fallback)`: the `--name=value` options the app started with |
| `qml/Speecher/Controls/` | The design system: buttons, switches, rows, cards, pages |
| `qml/Speecher/App/` | The window, sidebar, search palette and pages, and `ThemeSetting`, which makes Theme follow the Theme setting |
| `qml/Speecher/App/pages/` | One file per page, local parts in `pages/<area>/` |
| `qml/Speecher/Popup/` | The dictation popup |
| `qml/Speecher/Wording/` | The UI's wording: `Schema` (pages and settings rows, from `schema/*.js`) and `Words` (shared words and the shell's) |
| `qml/Speecher/Demo/` | `DemoVoice`: a made-up voice level for illustrations, previews and the mock backend |
| `mock/Speecher/Backend/` | The preview's `Speecher.Backend`: mock singletons standing in for core. The app ships all but `Settings`, `Status`, `Dictation`, `DictationState`, `Updates`, `Transcription`, `System`, `Models`, `Insights`, `Accounts` and `Audio` as placeholders |
| `host/` | The app's side (C++): `QuickFrontEnd`, which loads the windows in the daemon, `QuickPopupWindow`, which carries the popup, and the real `Settings`, `Status`, `Dictation`, `DictationState`, `Updates`, `Transcription`, `System`, `Models`, `Insights`, `Accounts` and `Audio` |
| `assets/` | Atkinson Hyperlegible Next, and the Lucide icon font |
| `preview/` | Screenshot and video tool, and the component gallery (`preview/qml/SpeecherPreview/Gallery.qml`) |

`cmake/QuickUi.cmake` builds each directory under `qml/` (and the Backend from
`mock/` plus `host/`) into a QML module compiled into the binary; each
directory's qmldir names its singletons for both the app and the preview.

## Wiring plan

Pages import `Speecher.Backend` and read only its singletons for data. The
preview's module in `mock/` and the app's module expose the same API, so
wiring an area means replacing its mock with a C++ type of the same name, not
editing pages. Each Backend type comes from exactly one module: the app's
Backend is the C++ types plus the mocks not yet replaced.

- Wired: `Settings` (`host/QuickSettings`), `Status` (`host/QuickStatus`),
  `Dictation` and `DictationState` (`host/QuickDictation`), `Updates`
  (`host/QuickUpdates`), `Transcription` (`host/QuickTranscription`),
  `System` (`host/QuickSystem`), `Models` (`host/QuickModels`), `Insights`
  (`host/QuickInsights`), `Accounts` (`host/QuickAccounts`) and `Audio`
  (`host/QuickAudio`). Placeholder until its wiring PR: `Library`.
- `System.checks` are `{id, state, reason, fix, failures}`; `Health`
  (health.js) words each by id and reason, and a failed fix by its steps.
  Bindings look a check up with `Health.find(System.checks, id)`, so they
  follow the checks. `System.fix(id)` runs the fix (`fixFromSetup` in the
  setup assistant). The virtual keyboard turns on only after its typing test
  (`VirtualKeyboardTest`) puts core's test text in the field.
  `System.pasteRoute` is the steps core's delivery takes (insert, press,
  copy). The phone code lives from `startPhoneTransfer()` to
  `stopPhoneTransfer()`, and `QrCode` draws it through the host's
  `image://qr` provider. `uninstall` reports in `System.removal` by part and
  outcome, and `RemovalReport` words it: under General's row, and alone in
  Main once the settings are deleted. The paste rules are Settings rows: `globalPasteRule`,
  `categoryPasteRule_<type>` (`inherit` is no rule of its own) and
  `applicationPasteRules`, a list of `{app, method, enabled}`.
- `Models` is what core knows about the providers and running models here,
  as data: speech services and refinement providers with a `state` and
  core's `problem` sentence, their ratings as `{ measure, value }` bars, the
  Local Model catalog with fits, speeds, downloads and Speed Tests, the
  hardware, the Local Runners and the Ollama pull, both Custom Endpoints'
  checks and setup's fallback offers. `QuickModels.h` documents each field.
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
- `Accounts`: each service's sign-in is core's check, run on a worker; a
  secret row is write-only: `Accounts.saveSecret` and `clearSecret` go
  through the daemon's `SecretStore`, and QML sees only whether a key is
  saved and why a save or clear failed. The speech providers' sign-ins
  (Codex, Claude Voice) have one owner, core's `ProviderAvailability`:
  `Models` shows them, setup checks them, and renewing a sign-in on Accounts
  checks them again.
- `Settings.values[id]` and `Settings.set(id, value)` are the settings store,
  keyed by the schema's row ids. One table in `QuickSettings.cpp` maps each id
  to its `AppSettings` field, and `set` saves through the daemon's
  `SettingsStore`, so the CLI and the daemon see the same values. A row the
  table lacks reads as absent, and SchemaRow greys it out where
  `Settings.saves(id)` is false; add the row to the table when its page is
  wired. A row whose choices come from a placeholder area waits for that
  area. The shortcut rows go to the controller, which binds and keeps them,
  and show the keys the controller displays. While one records,
  `Settings.startRecordingShortcut(id)` and `stopRecordingShortcut(id)` make
  the bound shortcuts let go of their keys. Where the desktop picks the keys
  (`Settings.desktopPicksKeys(id)`, the portal), the row records nothing and
  `Settings.act(id)` opens the desktop's chooser.
- `Settings.errors[id]` says why a row's last save was refused, such as a
  shortcut another app owns. SchemaRow shows it in place of its help; a page
  that gives a shortcut row its own help sets `note`, not `help`.
- `Settings.act(id)` runs an action row (`runSetup`).
- `Status` has the version and the shortcut's keys.
- `Updates` is the updater as `QuickUpdates.h` documents it: its state
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
  of objects) so a C++ `QObject` or model can replace them one for one.
  Each list has one owner (speech services and refinement providers in `Models`, profiles, tones and
  levels in `Library`, microphones in `Audio`, app types in the pasting
  schema); every other place derives from it with the same ids. A button
  that would leave the app (open settings, pick a file) calls a named mock
  command whose outcome shows: a state change, or `System.open(what)`'s toast.
  Mocks never import `Speecher.Controls`: `System.announced(text, icon)` says
  what happened, and Main shows it as a toast.
- The host (`QuickFrontEnd`) opens Main and the setup assistant when the
  daemon asks, at most one of each, and hides a window when it is closed.
  It creates `ThemeSetting` before any window, so every window follows the
  Theme setting.
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
  A C++ Backend type carries data, not wording; when an area is wired, the
  wording its mock holds moves into QML. The popup words itself
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
| `negative` | `#D23F34` | The margin's red: live sound, failures, and the popup's margin rule |

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

## Consistency rules

One thing looks and behaves one way everywhere. Fix a difference in the
shared control or Theme, not in a page.

Choosing:

- A provider or service is chosen with `ChoiceRows`, in settings, setup and
  Transcribe alike: icon, name over help, status on the right, a check after
  the chosen one. Ratings go in a row under the choice, for the chosen one.
  A profile's own override stays a `Select` with an explicit Default option.
- A check means a committed choice. The editor that is open (a profile in
  the list) is a selected `ListRow`, with no check.
- Two to four short options are segments; more, or a list that grows, is a
  `Select` (`CompactChoice` picks for you; SchemaRow does the same). `Chip`s
  are for choosing several at once or inserting a snippet.
- `SettingRow` for settings, `ListRow` for data. Rows start their content
  `Theme.rowInset` in from the card and end it the same distance from the
  right.

Press, hover, selection:

- Hover and press are colour changes over `Theme.fast`: `fill` under the
  pointer, `fillStrong` while pressed (`surfaceHover`/`surfacePressed` on
  raised buttons and fields). Nothing grows under the pointer.
- Only a button or a whole-card choice shrinks when pressed: text buttons
  and chips to 0.965, icon buttons to 0.92, `OptionCards` cards and theme
  thumbnails to 0.98, over `Theme.fast` springy. Rows, menus, segments,
  fields and the header of a card that opens never shrink.
- A card that opens is a `Card` whose header has the row wash and a chevron
  that turns over `Theme.base`; its body slides open. Opening never selects
  it. `SettingRow { clickable }`, `Disclosure` and ModelCard work this way.
- A selection glides to the new item over `Theme.slow` springy. A selected
  card keeps its 1 px border and changes its colour and check.
- A control that is disabled fades to `Theme.disabledOpacity`, easing. The
  control owns the fade; a row fades its own text and the control in it
  fades itself, so nothing fades twice. Something inactive but still
  editable stays legible and says why in its help; it is not dimmed.
- The focus ring shows for keyboard focus only: `visualFocus` on Templates
  controls, `FocusVisible` on custom ones. A text field shows its soft
  focus halo while it is being edited. A custom control that acts has Tab,
  Return and Space, and the right `Accessible.role`.
- Exceptions, named by role: a slider's handle grows while held, a dragged
  fallback row lifts to 1.02, a switch's knob glides, the popup's round
  buttons never take focus. Do not copy them elsewhere.

Shape:

- Radii: `radiusSmall` 4 for small shapes, `radius` 6 for controls,
  `radiusLarge` 8 for cards, panels and menus, `radiusPanel` 12 for the
  sheet and the popup. A shape set concentrically inside another takes the
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
  top and right, in the main window and setup alike. The sheet starts under
  the strip; the 14 px shadow room around a framed window goes when it is
  maximized and is not padding.

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
`Theme.slow`, `Theme.page` or `Theme.ms(n)`; curves from `Theme.easeOut`
(the default), `Theme.easeInOut`, `Theme.springy`, `Theme.easeIn`,
`Theme.breathe` (loops that pulse) or `Theme.steady` (motion that never
settles, such as a sweep). Timer intervals use `Theme.ms(n)` too. The video
tool slows time through `Theme.timeScale`, so an animation that ignores it
stutters on video.

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

Every control below is in `Speecher.Controls`, and the gallery shows each in
its states, with a few exceptions. `Card` and `Surface` show only as the
Sections' cards and the open menu. `RowList` looks like a Column until its
values change, so it and the helpers show only on the pages that use them.

Controls never import `Speecher.Backend`: the page reads the singletons and
hands the control plain values and options, and its wording too, since
controls have no words of their own (`Words` has the shared ones). Choice
controls show `currentId` and ask for a change through their signal; the
caller sets `currentId`, from the store or from its own state. A disabled
control fades to `Theme.disabledOpacity`.


Layout and rows:

- `Page { title; subtitle; headerActions }`: a scrolling page. With no title the page brings its own heading.
- `Section { title; caption; actions; plain; cardPadding }`: a heading over a raised card. Two side by side line up with `minimumHeaderHeight`/`minimumCardHeight` set from each other's `naturalHeaderHeight`/`naturalCardHeight`. Sections stacked outside a Page (in a Reveal or a Switcher view) go in a `Column { spacing: Theme.sectionGap }`.
- `Card`: the raised card on its own. `Surface`: a floating panel (menus, palettes). `Plate`: the recessed panel a page's or setup step's picture sits on.
- `SettingRow { title; help; iconName; stacked; clickable; trailingText; shown; first; highlight() }`: one setting with its control on the right.
- `SchemaRow { rowId }` (in Speecher.App): a SettingRow built from the schema.
- `ListRow { title; detail; iconName; titleVariant; wrapTitle; detailLines; first; clickable; selected; trailing; actions }`: a data row, not a setting. `trailing` always shows (a value, a status); `actions` are buttons that show on hover or focus. Rows that need more lay out their own content in the default slot.
- `RowList { values; keyOf; entry(key); first }`: a non-scrolling list of rows that animates adds, removes and moves. Delegates take `required property string key`, read `list.entry(key)` (still there while a row fades out) and set `first: index === 0 && list.first`.
- `Disclosure { title; help; trailingText; open; highlight() }`: a row that opens to more rows.
- `Reveal { open; sideways }`: content that opens and closes in place. Rows inside draw their divider unless the Reveal starts its card, so no `first: false` is needed.
- `Switcher { currentIndex }`: one of several views, sliding between them. `width: implicitWidth` sizes it to the current view, for buttons that swap in a row.
- `FormField { label; help }`: a label and help around one input in a form.
- `InlineForm { open; title; saveText; canSave }`: an add or edit form in place, instead of a dialog.
- `InlineConfirm { open; text; detail; confirmText; variant; leading }`: the question before an action that can't be undone: always a question and what it costs; leading (a check box) goes under them.
- `EmptyState { iconName; title; text; compact }`: what an empty list says. compact on a card, centred elsewhere.

Choosing:

- `Switch`, `CheckBox`, `Slider { format }`.
- `SegmentedControl { options; currentId; activated(id) }`: two to four short choices in a row.
- `Select { options; currentId; placeholder; chosen(id) }`: a dropdown.
- `CompactChoice { options; currentId; stretch; chosen(id) }`: segments for up to four options, a Select for more.
- `OptionCards { options; currentId; activated(id) }`: a few choices as cards with help.
- `ChoiceRows { options; currentId }`: a provider or service as rows on a card, each with its icon, help, status and a check when chosen.
- `Chip { text; iconName; checkable }`: small choices or snippet buttons in a Flow.
- `FallbackList { ids; options; statusOf; addLabel; emptyText }`: an ordered list to drag, reorder, add to and remove from.
- `EditableList { items; emptyText; formOpen }`: things a person made, with edit and delete.

Input:

- `TextField { iconName; variant; secret; trailing }`, `SearchField`, `SuggestField { suggestions; committed }`, `TextArea { mono; minHeight }`: all draw `InputFrame`. `variant: "title"` sets a name edited where a title would be. Fields default to `Theme.fieldWidth` and `Select` to `Theme.selectWidth`; leave them at that in settings rows.
- `ShortcutField { keys; emptyText; listeningText; clearText; escapeRecords; recorded(keys) }`, and `KeyCombo { keys; size; pressed; lit }` / `KeyCap { text; size }` to show keys.

Actions and menus:

- `Button { variant; size; iconName; trailingIcon; trailing; busy; tooltip }`, `IconButton { iconName; tooltip }`, `TextLink { text; iconName; variant }` for a link in a line of text. `trailing` ends the button with an item, such as the `KeyCombo { size: "small" }` of its key.
- `CopyButton { text; tooltip; copiedText; copied() }`: icon-only without text; the caller copies on `copied`.
- `Menu { items; currentId; picked(id) }`: a menu declared inside what opens it. `MenuButton { label; items; confirm(text) }` is a Button with one. `MenuItem` and `MenuPopup` are their parts, shared with Select.

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
- `AppIcon { name; iconName }`, `Icon { name }`, `BrandMark` (Speecher's own mark).

Helpers:

- `Grow on implicitHeight {}`: a size that eases once laid out.
- `Caret { edit }`: the blinking pencil caret after a TextEdit's last character, declared inside the edit.
- `RichText.escape(s)`: text made safe to put in rich text, before adding markup.

In Speecher.App, besides SchemaRow:

- `SearchLanding { rowIds; target; landed() }`: where a search for schema rows lands when no SchemaRow stands for them (a custom row, a Disclosure, a card or a section). It scrolls to target and emits landed so the target can flash or open.
- `AccessibilityNotice { text }`: the note at the top of a card whose rows need desktop accessibility.
- `HealthRow { rowId; checkId }`: a schema row showing one of `System.checks` with its fix; items inside follow the fix button. `StateText { stateId }` words a sign-in or check state, and `States` (states.js) maps states to tones.
- `Sheet { side }`: the inset sheet a window shows its pages on, beside its side column.
- `Brand { title; detail }`: the mark and the window's name in the title strip, for the main window and setup.

## Building pages

- `Page { title; subtitle; headerActions: [...] }` holds `Section`s. Its
  title is `Schema.page(id).title`, and a Section holding schema rows takes
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
- Preview states a page opens with (a form, a tab) come from
  `Launch.arg(...)`; mock singletons read their states the same way.
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

## Trying it by hand

`preview/try.sh` (or `preview/try.ps1` on Windows) opens the front end in an
ordinary window on mock data, to click around: `try.sh` for the main window,
`try.sh setup`, `try.sh popup` (the popup's story on a loop) and
`try.sh gallery`. It needs only uv; the first run fetches PySide6. Arguments
such as `--theme=dark` or `--reduce-motion=true` pass through.
`python preview/bundle.py OUT.zip` packs it to try on another machine.

## Previewing

```sh
preview/run.sh --page <page id> --out ~/.cache/speecher-quick/shots/<name>.png
preview/run.sh --page <page id> --theme light --out …
preview/run.sh --page <page id> --row <row id> --out …       # jump to a row
preview/run.sh --page <page id> --search "query" --out …     # search palette
preview/run.sh --page <page id> --arg transcribe-state=running --out …  # mock state
preview/run.sh --root SpeecherPreview/PopupScene --size 1200x760 --arg popup-demo=review --out review.png
preview/run.sh --root SpeecherPreview/PopupScene --size 1200x760 --record 19 --time-scale 3 --out popup.mp4
preview/run.sh --page vocabulary --record 2 --time-scale 4 --poke vocabularyPage.tab=corrections@0.6 --out tab.mp4  # an interaction, on video
preview/run.sh --page home --poke Speecher.Backend/Dictation.state=listening@0.5 --out live.png  # a singleton
preview/run.sh --page home --record 2 --poke Speecher.App/Navigation.current=refinement@0.5 --out load.mp4  # a page loading
preview/run.sh --root Speecher.App/SetupAssistant --size 960x720 --record 2 --poke step=3@0.5 --out step.mp4  # the window itself
preview/run.sh --page pasting --scroll 900 --out lower.png   # scrolled down first
```

`--poke` reaches an item by objectName (list delegates included), a
singleton as `Module/Type`, or the window when the name is left out.

`--arg name=value` reaches QML as `--name=value` in `Qt.application.arguments`;
mock singletons read it to start in a given state.

The gallery of every shared control, in either theme:

```sh
preview/run.sh --root SpeecherPreview/Gallery --size 1640x1600 --theme dark --out gallery.png
```

It runs a private headless KWin, so nothing appears on screen, and writes a
2x PNG. Lines starting `QML:` are warnings; fix all of them. A page is done
when you have looked at its screenshots in both themes yourself.

The app has one hook of its own for the same job: with
`SPEECHER_GRAB_MAIN_WINDOW=<file.png>` set, the daemon saves the real main
window a moment after each time it shows it, since a headless compositor lets
no other client take a screenshot. `packaging/verify-appimage.sh` uses it to
check that the packaged UI opens.
