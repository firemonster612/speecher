pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Window
import Speecher.Theme
import Speecher.Controls
import Speecher.Demo
import Speecher.Wording

// Every shared control in its states, for picking the right one and for
// checking a change to one of them in both themes:
//   preview/run.sh --root SpeecherPreview/Gallery --size 1640x1600 --out gallery.png
Window {
    id: gallery
    width: 1640
    height: 1600
    visible: true
    title: "Controls"
    color: Theme.sheet

    // A labelled group of specimens: the control's name above, the
    // specimens on a card.
    component Specimen: Section {
        cardPadding: Theme.space4
    }
    component Specimens: Flow {
        width: parent.width
        spacing: Theme.space2
    }

    readonly property var choices: [
        { id: "codex", label: "ChatGPT", help: "Very accurate, around 100 languages.", icon: "openai", status: "Ready", tone: "positive",
          bars: [{ label: "Accuracy", value: 9.2 }, { label: "Speed", value: 7 }] },
        { id: "claude", label: "Claude", help: "Streams as you speak.", icon: "claude", status: "Signed out", tone: "warning",
          bars: [{ label: "Accuracy", value: 8.4 }, { label: "Speed", value: 9 }] },
        { id: "local", label: "Local Model", help: "Nothing leaves the machine.", icon: "cpu", status: "Ready", tone: "positive", bars: null },
        { id: "none", label: "None", help: "Pastes your words as spoken.", icon: "circle-off" }
    ]
    readonly property var edit: [
        { kind: "removed", text: "um so " }, { kind: "same", text: "I think we should " },
        { kind: "removed", text: "tuesday no " }, { kind: "same", text: "meet " },
        { kind: "added", text: "Wednesday" }, { kind: "same", text: "." }
    ]
    property var fallbacks: ["claude", "openai"]
    property var tones: [{ id: "warm", title: "Warm", detail: "Friendly, with contractions." }, { id: "terse", title: "Terse", detail: "As few words as it takes." }]

    Row {
        x: Theme.space6
        y: Theme.space6
        spacing: Theme.space6

        // Actions and input ---------------------------------------------------
        Column {
            width: 372
            spacing: Theme.space5

            Specimen {
                title: "Button, CopyButton, MenuButton, TextLink"
                Specimens {
                    Button { text: "Primary"; variant: "primary" }
                    Button { text: "Secondary" }
                    Button { text: "Ghost"; variant: "ghost" }
                    Button { text: "Delete"; variant: "danger"; iconName: "trash-2" }
                    Button { text: "Testing"; busy: true; size: "small" }
                    Button { text: "Small"; size: "small"; trailingIcon: "external-link" }
                    Button { text: "Disabled"; enabled: false }
                    CopyButton { copiedText: Words.copied; tooltip: "Copy" }
                    CopyButton { copiedText: Words.copied; text: "Copy text"; size: "small" }
                    MenuButton { label: "Export"; leadingIcon: "download"; items: [{ id: "txt", label: "Text", icon: "file-text" }] }
                    TextLink { text: "Insights settings" }
                }
            }
            Specimen {
                title: "Chip, SegmentedControl, Switch, CheckBox, Slider"
                Column {
                    width: parent.width
                    spacing: Theme.space3
                    Specimens {
                        Chip { text: "Warm"; checkable: true; checked: true }
                        Chip { text: "Terse"; checkable: true }
                        Chip { text: "{date}"; iconName: "calendar" }
                    }
                    SegmentedControl { options: [{ id: "a", label: "Terms" }, { id: "b", label: "Replacements" }, { id: "c", label: "Corrections" }]; currentId: "b"; onActivated: id => currentId = id }
                    Row {
                        spacing: Theme.space4
                        Switch { checked: true }
                        Switch {}
                        CheckBox { text: "Checked"; checked: true }
                        CheckBox { text: "Not" }
                    }
                    Slider { width: parent.width; from: 0; to: 100; value: 40 }
                }
            }
            Specimen {
                title: "TextField, SearchField, SuggestField, TextArea, FormField, Select"
                Column {
                    width: parent.width
                    spacing: Theme.space3
                    TextField { width: parent.width; text: "Meeting notes"; variant: "title" }
                    TextField { width: parent.width; placeholderText: "Placeholder"; iconName: "app-window" }
                    TextField { showText: Words.show; hideText: Words.hide; width: parent.width; text: "sk-ant-0123456789"; secret: true }
                    SearchField { clearText: Words.clear; width: parent.width; text: "konsole" }
                    SuggestField { suggestionsText: Words.suggestions; width: parent.width; text: "gpt-6.1-sol"; suggestions: [{ id: "gpt-6.1-sol", label: "gpt-6.1-sol" }] }
                    TextArea { width: parent.width; mono: true; minHeight: 64; text: "You are a careful copy editor.\nKeep the speaker's words." }
                    FormField {
                        label: "Application ID"
                        help: "The app's desktop ID, such as org.kde.konsole."
                        TextField { width: parent.width; placeholderText: "org.kde.konsole" }
                    }
                    Select { width: parent.width; options: gallery.choices; currentId: "claude"; onChosen: id => currentId = id }
                }
            }
            Specimen {
                title: "KeyCombo, KeyCap, ShortcutField"
                Column {
                    width: parent.width
                    spacing: Theme.space3
                    Row {
                        spacing: Theme.space4
                        KeyCombo { keys: ["Ctrl", "K"]; size: "small"; anchors.verticalCenter: parent.verticalCenter }
                        KeyCombo { keys: ["Ctrl", "Space"]; anchors.verticalCenter: parent.verticalCenter }
                        ShortcutField { emptyText: Words.notSet; listeningText: Words.pressShortcut; clearText: Words.clearShortcut; keys: ["Meta", "H"]; anchors.verticalCenter: parent.verticalCenter }
                    }
                    KeyCombo { keys: ["Ctrl", "Space"]; size: "large"; pressed: true; lit: true }
                }
            }
            Section {
                title: "EditableList"
                EditableList { editText: Words.edit; removeText: Words.delete_; items: gallery.tones; emptyText: "No tones yet." }
            }
            Specimen {
                title: "Menu, open"
                caption: "Shared by Select, SuggestField and MenuButton."
                Item {
                    width: parent.width
                    height: 150
                    Button {
                        id: menuOpener
                        text: "Speech service"
                        trailingIcon: "chevron-down"
                        Menu {
                            id: openMenu
                            align: "left"
                            items: gallery.choices.map(c => ({ id: c.id, label: c.label, icon: c.icon, help: c.help }))
                            currentId: "local"
                        }
                    }
                }
            }
        }

        // Rows and lists ------------------------------------------------------
        Column {
            width: 396
            spacing: Theme.space5

            Section {
                title: "SettingRow, Disclosure, Notice"
                caption: "Rows on a card: inset dividers, controls on the right."
                SettingRow { title: "Pause media"; help: "While you dictate."; Switch { checked: true } }
                SettingRow { title: "Clickable row"; trailingText: "3"; clickable: true }
                Disclosure {
                    title: "Built-in matches"
                    help: "Common apps Speecher already knows."
                    trailingText: "42 apps"
                    open: true
                    SettingRow { title: "Inside a disclosure" }
                }
                Disclosure { title: "Closed disclosure"; trailingText: "Default" }
                Notice { inset: Theme.rowInset; text: "This microphone is unplugged."; actionText: "Use default" }
                Notice { inset: Theme.rowInset; indicator: "icon"; variant: "small"; tone: "info"; text: "An icon notice in small type, for why something is greyed out." }
            }
            Specimen {
                title: "StatusText, MorphText, Reveal sideways"
                Column {
                    spacing: Theme.space2
                    StatusText { text: "Signed in"; tone: "positive"; indicator: "dot" }
                    StatusText { text: "Sign-in expired"; tone: "warning" }
                    StatusText { text: "Not installed"; tone: "negative" }
                    StatusText { text: "Checking…"; busy: true }
                    StatusText { text: "Plain status, no dot" }
                    StatusText { text: "Waiting"; tone: "tertiary" }
                    StatusText { text: "Listening"; tone: "live" }
                    MorphText { id: counter; property int n: 1290; text: n + " words"; variant: "label" }
                    MorphText {
                        width: 300
                        wrap: true
                        text: "A note that wraps over two lines fades to its new wording, and the height eases to fit."
                        variant: "small"
                        tone: "secondary"
                    }
                    Row {
                        spacing: Theme.space2
                        StatusText { text: "Stopped"; tone: "warning"; anchors.verticalCenter: parent.verticalCenter }
                        // Folds away sideways, as Reveal { sideways } does.
                        Reveal { sideways: true; open: true; anchors.verticalCenter: parent.verticalCenter; Button { text: "Start"; size: "small" } }
                    }
                }
            }
            Section {
                title: "InlineConfirm, InlineForm"
                SettingRow { title: "Delete history"; help: "Removes every saved dictation."; Button { text: "Delete"; variant: "danger"; size: "small" } }
                InlineConfirm { cancelText: Words.cancel; open: true; text: "Delete 1,284 dictations?"; detail: "This can't be undone."; confirmText: "Delete" }
                InlineForm {
                    cancelText: Words.cancel
                    open: true
                    title: "New replacement"
                    saveText: "Add replacement"
                    FormField { label: "When you say"; TextField { width: parent.width; iconName: "mic"; text: "sig" } }
                }
            }
            Section {
                title: "ServiceCards"
                caption: "Every provider choice. Status takes the option's tone."
                ServiceCards { unratedText: "No rating"; options: gallery.choices.slice(0, 2); currentId: "codex"; onActivated: id => currentId = id }
            }
            Section {
                caption: "Disabled"
                ServiceCards { unratedText: "No rating"; options: gallery.choices.slice(2); currentId: "local"; enabled: false }
            }
        }

        // Choices, data and feedback ------------------------------------------
        Column {
            width: 376
            spacing: Theme.space5

            Section {
                title: "FallbackList, ListRow"
                FallbackList {
                    moveUpText: Words.moveUp
                    moveDownText: Words.moveDown
                    removeText: Words.remove
                    ids: gallery.fallbacks
                    options: [
                        { id: "claude", label: "Claude", icon: "claude", status: "Used when the first can't run" },
                        { id: "openai", label: "ChatGPT", icon: "openai", status: "No API key", tone: "negative" },
                        { id: "local", label: "Local model", icon: "cpu" }
                    ]
                    addLabel: "Add a fallback"
                    addPlaceholder: Words.choose
                    onEdited: ids => gallery.fallbacks = ids
                }
                ListRow {
                    iconName: "file-audio"
                    title: "standup-0412.m4a"
                    detail: "12 minutes, 1,840 words"
                    trailing: StatusText { text: "Waiting"; tone: "tertiary" }
                    actions: [IconButton { iconName: "pencil"; size: 28; tooltip: "Edit" }]
                }
            }
            Specimen {
                title: "OptionCards"
                OptionCards {
                    width: parent.width
                    options: [{ id: "paste", label: "Paste", icon: "clipboard", help: "Into the app you were in." }, { id: "type", label: "Type", icon: "keyboard", help: "Key by key." }]
                    currentId: "paste"
                    onActivated: id => currentId = id
                }
            }
            Specimen {
                title: "RatingBars"
                Column {
                    spacing: Theme.space4
                    RatingBars { bars: [{ label: "Accuracy", value: 8.5 }, { label: "Speed", value: 6 }, { label: "Price", value: null }]; color: Theme.accent }
                    RatingBars { appearance: "wide"; bars: [{ label: "Quality", value: 9 }, { label: "Speed", value: 4 }] }
                }
            }
            Specimen {
                title: "StepNode"
                Column {
                    spacing: Theme.space3
                    StepNode { number: 1; title: "Welcome"; stage: "done"; detail: "Shortcut set" }
                    StepNode { number: 2; title: "Accessibility"; stage: "warning"; detail: "Not turned on" }
                    StepNode { number: 3; title: "Transcription"; stage: "current" }
                    StepNode { number: 4; title: "Ready"; stage: "todo" }
                }
            }
            Specimen {
                title: "DiffText"
                caption: "marked, spoken, result."
                Column {
                    width: parent.width
                    spacing: Theme.space2
                    DiffText { segments: gallery.edit; variant: "transcript" }
                    DiffText { segments: gallery.edit; mode: "spoken"; variant: "small" }
                    DiffText { segments: gallery.edit; mode: "result"; variant: "small" }
                }
            }
            Specimen {
                title: "Plate, Caret"
                Plate {
                    height: 64
                    TextEdit {
                        id: typedSample
                        x: Theme.space5
                        anchors.verticalCenter: parent.verticalCenter
                        text: "The next words land here"
                        readOnly: true
                        color: Theme.text
                        font.family: Theme.fontFamily
                        font.pixelSize: Theme.fontTranscript
                        Caret { edit: typedSample }
                    }
                }
            }
            Specimen {
                title: "Waveform, ProgressBar, BrandMark, Spinner, Badge, TipBody"
                Column {
                    width: parent.width
                    spacing: Theme.space3
                    Waveform { width: parent.width; bars: 48; fitWidth: true; level: voice.level }
                    Waveform { width: parent.width; height: 48; bars: 60; fitWidth: true; compress: true; fadeOlder: true; threshold: 0.12; thresholdText: "Quiet"; level: voice.level }
                    ProgressBar { width: parent.width; value: 0.62 }
                    ProgressBar { width: parent.width; indeterminate: true }
                    Row {
                        spacing: Theme.space3
                        BrandMark { anchors.verticalCenter: parent.verticalCenter }
                        Spinner { anchors.verticalCenter: parent.verticalCenter }
                        StatusDot { pulse: true; color: Theme.negative; anchors.verticalCenter: parent.verticalCenter }
                        Badge { text: "Open now"; anchors.verticalCenter: parent.verticalCenter }
                        Badge { text: "Connected"; tone: "positive"; dot: true; anchors.verticalCenter: parent.verticalCenter }
                        TipBody { text: "Tuesday"; detail: "412 words" }
                    }
                }
            }
        }

        // Layout, pictures and tips ---------------------------------------------
        Column {
            width: 340
            spacing: Theme.space5

            Specimen {
                title: "Switcher"
                caption: "Beside a SegmentedControl, as tabs."
                Column {
                    width: parent.width
                    spacing: Theme.space3
                    SegmentedControl {
                        id: switcherTabs
                        options: [{ id: "first", label: "First" }, { id: "second", label: "Second" }]
                        currentId: "first"
                        onActivated: id => currentId = id
                    }
                    Switcher {
                        currentIndex: switcherTabs.currentIndex
                        Txt { text: "The first view."; tone: "secondary" }
                        Txt { text: "The second view slides in from its side."; tone: "secondary"; wrapMode: Text.WordWrap }
                    }
                }
            }
            Specimen {
                title: "AppIcon"
                Specimens {
                    AppIcon { name: "Slack" }
                    AppIcon { name: "Konsole" }
                    AppIcon { name: "Mail"; iconName: "mail" }
                }
            }
            Specimen {
                title: "Tip, PointTip"
                Item {
                    width: parent.width
                    height: 92
                    Button { y: 48; text: "Pointed at"; Tip { text: "A control's tooltip"; detail: "With a quieter line"; shown: true } }
                    Rectangle { id: tipBar; x: 250; y: 48; width: 18; height: 40; radius: Theme.radiusSmall; color: Theme.accent }
                    PointTip { shown: true; pointX: tipBar.x + tipBar.width / 2; pointY: tipBar.y; text: "Tuesday"; detail: "412 words" }
                }
            }
            Specimen {
                title: "Page"
                caption: "A scrolling page, here in a short frame."
                Page {
                    width: parent.width
                    height: 220
                    title: "Page title"
                    subtitle: "A line of explanation under it."
                    Section { title: "Section"; SettingRow { title: "A row" } }
                }
            }
            Specimen {
                title: "EmptyState"
                caption: "Centred, then compact on a card."
                EmptyState { iconName: "route"; title: "No fallbacks"; text: "Add one to try when the first can't run." }
            }
            Specimen {
                EmptyState { compact: true; text: "No tones yet. Add one to give a profile its voice." }
            }
        }
    }

    DemoVoice { id: voice }

    Toast {}

    Component.onCompleted: {
        openMenu.open()
        counter.n = 1291
        Toaster.show("Exported 3 transcripts", { icon: "check", tone: "positive", actionText: "Show" })
    }
}
