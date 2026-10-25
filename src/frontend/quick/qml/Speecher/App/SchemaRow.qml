pragma ComponentBehavior: Bound
import QtQuick
import Speecher.Controls
import Speecher.Backend
import Speecher.Wording

// A settings row built from its schema entry: label, help and the control its
// kind calls for, bound to the row's value in Settings. A secret is
// write-only: Accounts saves and clears it and says only whether one is saved.
SettingRow {
    id: root

    required property string rowId
    readonly property var spec: Schema.row(rowId)
    // A row that needs the desktop to blur behind windows shows off, greyed
    // and with why where it can't; the saved value comes back when it can.
    readonly property bool _blurs: !spec.needsBlur || Appearance.canBlur
    // What the control shows: the saved value, unless a page shows what is
    // in effect instead.
    property var value: _blurs ? Settings.values[rowId] : false

    // The row's help; a page may give its own. Why the row's last save was
    // refused shows in its place.
    property string note: (_blurs ? spec.help : spec.disabledHelp) || ""
    readonly property string _secretError: spec.kind === "secret" && rowId in Accounts.secretErrors
                                           ? Words.secretError(Accounts.secretErrors[rowId]) : ""

    title: spec.label
    help: Settings.errors[rowId] || _secretError || note
    iconName: spec.icon || ""
    // Usable once the backend saves the row or runs its action; a custom row
    // with no value draws content that has nothing to save. enabledWhen reads
    // Settings.values here, in the binding, so the row follows that value.
    enabled: _blurs && (spec.kind === "custom" && spec.value === undefined || spec.kind === "secret" || Settings.saves(rowId))
             && (!spec.enabledWhen || Settings.values[spec.enabledWhen.id] === spec.enabledWhen.equals)
    // The options this computer has; a row it doesn't have, or one whose
    // schema entry says hiddenWhen, folds away rather than greying.
    readonly property var _options: Schema.options(spec, need => System[need])
    shown: Schema.available(spec, need => System[need])
           && (!spec.hiddenWhen || Settings.values[spec.hiddenWhen.id] !== spec.hiddenWhen.equals)
    stacked: spec.kind === "choice" && spec.control === "cards"

    Loader {
        sourceComponent: {
            switch (root.spec.kind) {
            case "toggle": return toggle
            // Segments for two to four options, as CompactChoice picks.
            case "choice": return root.spec.control === "cards" ? cards
                                : root.spec.control === "select" || root._options.length > 4 ? select : segmented
            case "slider": return slider
            case "text": return textField
            case "secret": return secretField
            case "shortcut": return shortcut
            case "action": return action
            default: return null
            }
        }
        width: root.stacked ? parent.width : implicitWidth
    }

    Component { id: toggle; Switch { checked: !!root.value; onToggled: Settings.set(root.rowId, checked) } }
    Component {
        id: segmented
        SegmentedControl { options: root._options; currentId: root.value ?? ""; onActivated: id => Settings.set(root.rowId, id) }
    }
    Component {
        id: select
        Select { placeholder: Words.choose; options: root._options; currentId: root.value ?? ""; onChosen: id => Settings.set(root.rowId, id) }
    }
    Component {
        id: cards
        OptionCards { options: root._options; currentId: root.value ?? ""; onActivated: id => Settings.set(root.rowId, id) }
    }
    Component {
        id: slider
        Row {
            spacing: 12
            Txt {
                text: Schema.valueLabel(root.spec, knob.value)
                variant: "mono"
                tone: "secondary"
                width: 64
                horizontalAlignment: Text.AlignRight
                anchors.verticalCenter: parent.verticalCenter
            }
            Slider {
                id: knob
                from: root.spec.min; to: root.spec.max; stepSize: root.spec.step || 1
                value: root.value
                format: v => Schema.valueLabel(root.spec, v)
                // A drag saves once, when it lets go; each key press saves.
                onMoved: if (!pressed) Settings.set(root.rowId, value)
                onPressedChanged: if (!pressed && value !== root.value) Settings.set(root.rowId, value)
                anchors.verticalCenter: parent.verticalCenter
            }
        }
    }
    Component {
        id: textField
        TextField {
            text: root.value || ""
            placeholderText: root.spec.placeholder || ""
            onEditingFinished: Settings.set(root.rowId, text)
        }
    }
    Component {
        id: secretField
        TextField {
            id: field
            readonly property bool saved: Accounts.savedSecrets[root.rowId] === true
            // The key a refused clear left behind is still there to clear.
            readonly property bool clearable: saved || Accounts.pendingClears[root.rowId] === true
            // Whether the field changed since its draft went to Accounts:
            // a flag, so no copy of the key outlives the field's own text.
            property bool _editedSinceSubmit: false
            showText: Words.show
            hideText: Words.hide
            secret: true
            placeholderText: saved ? Words.savedSecret : root.spec.placeholder || ""
            // Once saved, the key leaves the field; a refused one stays to
            // retry. Typing goes on while the keyring answers, and a save
            // asked for meanwhile waits its turn, so the field lets go only
            // when Accounts says the draft submitted last was kept, and only
            // if nothing was typed since.
            onTextChanged: _editedSinceSubmit = true
            onEditingFinished: {
                if (!text.trim())
                    return
                _editedSinceSubmit = false
                Accounts.saveSecret(root.rowId, text)
            }
            Connections {
                target: Accounts
                function onSecretSaved(rowId) {
                    if (rowId === root.rowId && !field._editedSinceSubmit)
                        field.clear()
                }
            }
            trailing: IconButton {
                visible: field.clearable && !field.length
                iconName: "x"
                size: 24
                tooltip: Words.clear
                onClicked: Accounts.clearSecret(root.rowId)
            }
        }
    }
    Component {
        id: shortcut
        ShortcutField {
            emptyText: Words.notSet; listeningText: Words.pressShortcut; clearText: Words.clearShortcut
            // The keys that act while dictating may be Escape itself.
            escapeRecords: root.rowId === "cancelShortcut" || root.rowId === "pauseShortcut"
            keys: root.value || []
            // Where the desktop picks the keys (the portal), clicking opens its
            // chooser, and nothing records or lets go here.
            recordsKeys: !Settings.desktopPicksKeys(root.rowId)
            onClicked: if (!recordsKeys) Settings.act(root.rowId)
            // The bound shortcuts let go while it listens, so pressing them
            // records them; every way listening ends takes them back.
            onListeningChanged: listening ? Settings.startRecordingShortcut(root.rowId) : Settings.stopRecordingShortcut(root.rowId)
            Component.onDestruction: if (listening) Settings.stopRecordingShortcut(root.rowId)
            onRecorded: keys => Settings.set(root.rowId, keys)
        }
    }
    Component {
        id: action
        Button {
            text: root.spec.actionLabel || ""
            iconName: root.spec.actionIcon || ""
            variant: root.spec.variant || "secondary"
            onClicked: Settings.act(root.rowId)
        }
    }

    SearchLanding { rowIds: [root.rowId]; target: root; onLanded: root.highlight() }
}
