pragma ComponentBehavior: Bound
import QtQuick
import Speecher.Theme
import Speecher.Controls
import Speecher.Backend
import Speecher.Wording
import Speecher.App
import "text"

// How words get into apps: the delivery steps in the order core tries
// them, each with what it relies on here; then the options, and the
// exceptions that narrow the steps for an app type or a single app.
Page {
    id: page
    title: Schema.page("pasting").title
    subtitle: Schema.page("pasting").subtitle

    readonly property var _steps: System.deliverySteps
    function _runs(id) { return _steps.some(s => s.id === id && s.on && s.ready) }
    readonly property bool _keeps: _steps.some(s => s.id === "keep" && s.on)
    // The steps that are on, in words, each marked where this computer
    // can't run it yet: [{ icon, label, ready }].
    readonly property var _route: _steps.filter(s => s.on).map(s => Object.assign({ id: s.id, ready: s.ready }, ({
        insert: { icon: "text-cursor-input", label: "Insert" },
        press: { icon: "keyboard", label: "Copy, press " + Schema.pasting.pasteKeys },
        keep: { icon: "clipboard", label: "Keep on clipboard" }
    })[s.id]))
    // Whether the words can get past the clipboard into the app.
    readonly property bool _reachesApp: _runs("insert") || _runs("press")
    readonly property string _routeCaption: {
        // What the steps that are on but can't run yet wait for, by name.
        const waiting = [...new Set([].concat(..._steps.filter(s => s.on && s.id !== "keep").map(s => s.waiting))
                                      .map(n => Schema.pasting.needs[n].label))].join(" or ")
        if (!_reachesApp && _keeps)
            return waiting ? "Your words stay on the clipboard until " + waiting + " is ready."
                           : "Your words stay on the clipboard for you to paste."
        if (!_reachesApp)
            return "Nothing delivers your words until " + waiting + " is ready."
        // Insertion writes plain text whatever the format.
        const formatted = Settings.values.outputFormat === "html"
        const delivers = !formatted || !_runs("press") ? "Delivers plain text"
                       : _runs("insert") ? "Plain text when inserted, formatted when pasted" : "Delivers formatted text"
        if (_keeps) return delivers + ", and leaves a copy on your clipboard."
        if (!_runs("press")) return delivers + ", and leaves your clipboard alone."
        return delivers + ". After a paste your clipboard goes back, unless Speecher can't confirm the paste, which leaves your words there."
    }
    // Whether the steps can replace selected text: Linux inserts without
    // replacing a selection, so there only the paste keys can.
    readonly property bool _editsSelections: Qt.platform.os === "linux" ? _runs("press") : _reachesApp

    Section {
        plain: true
        PasteRoute {
            steps: page._route
            caption: page._routeCaption
            appName: page._reachesApp ? "Slack" : ""
            appCaption: "For example"
            clipboardLabel: "Your clipboard"
            // Words that stay on the clipboard show there instead.
            sample: page._reachesApp || page._keeps ? "Can you send me last quarter's numbers by Friday?" : ""
            startLabel: "Your words"
            firstLabel: "First"
            fallbackLabel: "If that fails"
            keptLabel: "Also"
        }
    }

    Section {
        title: Schema.row("insertThroughAccessibility").section
        caption: "Speecher tries these in order. Turn off any you don't want; one always stays on."
        DeliveryStepRow {
            rowId: "insertThroughAccessibility"
            AccessibilityMenu {}
        }
        DeliveryStepRow {
            rowId: "pressPasteKeys"
            // The virtual keyboard's own menu, once it is set up.
            Reveal {
                sideways: true
                open: System.hasVirtualKeyboard && System.virtualKeyboardActions.length > 0
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
        DeliveryStepRow { rowId: "keepOnClipboard" }
    }

    Section {
        title: Schema.row("outputFormat").section
        SchemaRow { rowId: "outputFormat" }
        SchemaRow {
            rowId: "reviewSelectionEdits"
            // A saved preference the steps can't act on says so.
            note: spec.help + (!page._editsSelections && Settings.values.reviewSelectionEdits
                ? " " + (Qt.platform.os === "linux"
                         ? "Your steps can't replace selected text now: on Linux that needs Press the paste keys, ready and on."
                         : "Your steps can't replace selected text now: neither Insert nor Press can run.")
                : "")
        }
    }

    AppPasteRules {}

    SubpageRows { pageId: "pasting" }
}
