pragma ComponentBehavior: Bound
import QtQuick
import Speecher.Controls
import Speecher.Backend

// A settings row built from its schema entry: label, help and the control its
// kind calls for, bound to the row's value in Settings.
SettingRow {
    id: root

    required property string rowId
    readonly property var spec: Schema.row(rowId)
    readonly property var value: Settings.values[rowId]

    title: spec.label
    help: spec.help || ""
    iconName: spec.icon || ""
    enabled: Settings.rowEnabled(spec)
    // A row whose schema entry says hiddenWhen folds away rather than greying.
    shown: !spec.hiddenWhen || Settings.values[spec.hiddenWhen.id] !== spec.hiddenWhen.equals
    stacked: spec.kind === "choice" && spec.control === "cards"

    Loader {
        sourceComponent: {
            switch (root.spec.kind) {
            case "toggle": return toggle
            case "choice": return root.spec.control === "cards" ? cards
                                : root.spec.control === "select" || root.spec.options.length > 3 ? select : segmented
            case "slider": return slider
            case "text": case "secret": return textField
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
        SegmentedControl { options: root.spec.options; currentId: root.value; onActivated: id => Settings.set(root.rowId, id) }
    }
    Component {
        id: select
        Select { placeholder: Words.choose; options: root.spec.options; currentId: root.value; onChosen: id => Settings.set(root.rowId, id) }
    }
    Component {
        id: cards
        OptionCards { options: root.spec.options; currentId: root.value; onActivated: id => Settings.set(root.rowId, id) }
    }
    Component {
        id: slider
        Row {
            spacing: 12
            Txt {
                text: Schema.valueLabel(root.spec, root.value)
                variant: "mono"
                tone: "secondary"
                width: 64
                horizontalAlignment: Text.AlignRight
                anchors.verticalCenter: parent.verticalCenter
            }
            Slider {
                from: root.spec.min; to: root.spec.max; stepSize: root.spec.step || 1
                value: root.value
                format: v => Schema.valueLabel(root.spec, v)
                onMoved: Settings.set(root.rowId, value)
                anchors.verticalCenter: parent.verticalCenter
            }
        }
    }
    Component {
        id: textField
        TextField {
            showText: Words.show
            hideText: Words.hide
            secret: root.spec.kind === "secret"
            text: root.value || ""
            placeholderText: root.spec.placeholder || ""
            onEditingFinished: Settings.set(root.rowId, text)
        }
    }
    Component {
        id: shortcut
        ShortcutField {
            emptyText: Words.notSet; listeningText: Words.pressShortcut; clearText: Words.clearShortcut
            // The keys that act while dictating may be Escape itself.
            escapeRecords: root.rowId === "cancelShortcut" || root.rowId === "pauseShortcut"
            keys: root.value || []
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
