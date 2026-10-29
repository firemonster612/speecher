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
    // Without accessibility nothing is inserted, so the route leaves that out.
    readonly property var _route: System.pasteRoute.filter(step => step !== "insert" || System.accessibility).map(step => ({
        insert: { icon: "text-cursor-input", label: "Insert directly" },
        // macOS pastes with ⌘V, terminals included.
        press: { icon: "keyboard", label: Qt.platform.os === "osx" ? "Press ⌘V"
                                        : Settings.values.globalPasteRule === "terminal_paste" ? "Press Ctrl+Shift+V" : "Press Ctrl+V" },
        copy: { icon: "clipboard", label: "Copy to clipboard" }
    })[step])
    // Where words only reach the clipboard, what would get them further, as
    // the default paste rule and Paste with need it.
    readonly property string _routeCaption: System.wordsStayCopied
        ? "Your words stay on the clipboard until " + ({ accessibility: "desktop accessibility is on.",
                                                         keyboard: "the virtual keyboard is set up.",
                                                         either: "desktop accessibility or the virtual keyboard is set up." })[
              System.pasteNeed]
        : (Settings.values.outputFormat === "html" ? "Pastes formatted text" : "Pastes plain text")
          + (Settings.values.restoreClipboardAfterTyping ? ", then puts back what was on your clipboard." : " and leaves it on your clipboard.")

    Section {
        plain: true
        PasteRoute {
            steps: page._route
            caption: page._routeCaption
            appName: "Slack"
            // Words that stay on the clipboard never reach the app.
            sample: System.wordsStayCopied ? "" : "Can you send me last quarter's numbers by Friday?"
            startLabel: "Your words"
            firstLabel: "First"
            fallbackLabel: "If that fails"
        }
    }

    Section {
        title: Schema.row("outputMethod").section
        AccessibilityNotice { text: "Turn on desktop accessibility so Speecher can insert your words directly." }
        SchemaRow { rowId: "outputMethod" }
        // The same check as Permissions & health's, with the setup's own menu after it.
        HealthRow {
            rowId: "virtualKeyboard"
            checkId: "virtualKeyboardHealth"
            // The menu's room folds away while it has nothing to offer, so
            // the fix button ends on the row's inset like every control.
            Reveal {
                sideways: true
                open: System.virtualKeyboardActions.length > 0
                anchors.verticalCenter: parent.verticalCenter
                IconButton {
                    iconName: "ellipsis"
                    size: 28
                    tooltip: Words.more
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
        }
        VirtualKeyboardTest {}
        SchemaRow { rowId: "globalPasteRule" }
        SchemaRow { rowId: "outputFormat" }
        SchemaRow { rowId: "restoreClipboardAfterTyping" }
    }

    SubpageRows { pageId: "pasting" }
}
