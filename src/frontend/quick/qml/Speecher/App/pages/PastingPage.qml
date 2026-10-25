pragma ComponentBehavior: Bound
import QtQuick
import Speecher.Controls
import Speecher.Backend
import Speecher.Wording
import Speecher.App
import "text"

Page {
    id: page
    title: Schema.page("pasting").title
    subtitle: Schema.page("pasting").subtitle

    // The steps a paste takes with the default paste and Paste with, in the
    // order core's delivery tries them, in words: [{ icon, label }].
    readonly property var _route: System.pasteRoute.map(step => ({
        insert: { icon: "text-cursor-input", label: "Insert directly" },
        // macOS pastes with ⌘V, terminals included.
        press: { icon: "keyboard", label: Qt.platform.os === "osx" ? "Press ⌘V"
                                        : Settings.values.globalPasteRule === "terminal_paste" ? "Press Ctrl+Shift+V" : "Press Ctrl+V" },
        copy: { icon: "clipboard", label: "Copy to clipboard" }
    })[step])
    readonly property string _routeCaption: (Settings.values.outputFormat === "html" ? "Pastes formatted text" : "Pastes plain text")
        + (Settings.values.restoreClipboardAfterTyping ? ", then puts back what was on your clipboard." : " and leaves it on your clipboard.")

    Section {
        plain: true
        PasteRoute {
            steps: page._route
            caption: page._routeCaption
            appName: "Slack"
            sample: "Can you send me last quarter's numbers by Friday?"
            startLabel: "Your words"
            firstLabel: "First"
            fallbackLabel: "If that fails"
        }
    }

    Section {
        title: Schema.row("outputMethod").section
        SchemaRow { rowId: "outputMethod" }
        // The same check as General's, with the setup's own menu after it.
        HealthRow {
            rowId: "virtualKeyboard"
            checkId: "virtualKeyboardHealth"
            IconButton {
                iconName: "ellipsis"
                size: 28
                tooltip: Words.more
                enabled: System.virtualKeyboardActions.length > 0
                opacity: enabled ? 1 : 0
                anchors.verticalCenter: parent.verticalCenter
                Behavior on opacity { Ease {} }
                onClicked: keyboardMenu.open()
                Menu {
                    id: keyboardMenu
                    width: 240
                    items: [
                        { id: "disable", label: "Disable in Speecher", icon: "power-off", help: "Keeps the setup for later." },
                        { id: "remove", label: "Remove setup", icon: "trash-2", help: "Asks for administrator permission." }
                    ].filter(a => System.virtualKeyboardActions.includes(a.id))
                    onPicked: id => id === "remove" ? System.removeVirtualKeyboard() : System.disableVirtualKeyboard()
                }
            }
        }
        VirtualKeyboardTest {}
        SchemaRow { rowId: "globalPasteRule" }
        SchemaRow { rowId: "outputFormat" }
        SchemaRow { rowId: "restoreClipboardAfterTyping" }
    }

    Section {
        title: Schema.row("categoryPasteRule_terminal").section
        caption: "Paste differently into every app of one type. Apps are sorted into types under Application recognition below."
        AccessibilityNotice { text: "Turn on desktop accessibility so Speecher can tell which app you're in." }
        // Two columns of rows, so seven types don't make a tall stack.
        Row {
            id: typeColumns
            readonly property int half: Math.ceil(Schema.appCategories.length / 2)
            width: parent.width
            Repeater {
                model: [Schema.appCategories.slice(0, typeColumns.half), Schema.appCategories.slice(typeColumns.half)]
                Column {
                    id: typeColumn
                    required property var modelData
                    width: typeColumns.width / 2
                    Repeater {
                        model: typeColumn.modelData
                        SchemaRow {
                            required property var modelData
                            rowId: "categoryPasteRule_" + modelData.id
                            help: modelData.examples
                            enabled: System.accessibility
                        }
                    }
                }
            }
        }
    }

    AppPasteRules {}

    AppRecognitionRules { onRevealRequested: item => page.reveal(item) }
}
