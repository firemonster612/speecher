pragma ComponentBehavior: Bound
import QtQuick
import Speecher.Theme
import Speecher.Controls
import Speecher.Backend
import Speecher.Wording
import Speecher.App
import "text"

// How words get into apps: the delivery steps in the order core tries
// them, each with what it needs here; then the format, selection edits, and
// the rules that narrow the steps for an app type or a single app.
Page {
    id: page
    title: Schema.page("pasting").title
    subtitle: Schema.page("pasting").subtitle

    readonly property var _steps: System.deliverySteps
    readonly property bool _keeps: _steps.some(s => s.id === "keep" && s.on)
    // The steps that are on, in words, each marked where this computer
    // can't run it yet: [{ icon, label, ready }].
    readonly property var _route: _steps.filter(s => s.on).map(s => Object.assign({ id: s.id, ready: s.ready }, ({
        insert: { icon: "text-cursor-input", label: "Insert" },
        press: { icon: "keyboard", label: "Copy, press " + Schema.pasting.pasteKeys },
        keep: { icon: "clipboard", label: "Keep on clipboard" }
    })[s.id]))
    // Whether the words can get past the clipboard into the app.
    readonly property bool _reachesApp: _steps.some(s => s.id !== "keep" && s.on && s.ready)
    readonly property string _routeCaption: {
        // What the steps that are on but can't run yet need, by name.
        const waiting = [...new Set(_steps.filter(s => s.on && !s.ready && s.id !== "keep")
                                          .map(s => Schema.row(Schema.pasting.rowOf[s.id]).need.label))].join(" or ")
        if (!_reachesApp && _keeps)
            return waiting ? "Your words stay on the clipboard until " + waiting + " is ready."
                           : "Your words stay on the clipboard for you to paste."
        if (!_reachesApp)
            return "Nothing delivers your words until " + waiting + " is ready."
        return (Settings.values.outputFormat === "html" ? "Delivers formatted text" : "Delivers plain text")
            + (_keeps ? " and leaves it on your clipboard." : ", then puts back what was on your clipboard.")
    }

    Section {
        plain: true
        PasteRoute {
            steps: page._route
            caption: page._routeCaption
            appName: page._reachesApp ? "Slack" : ""
            appCaption: "For example"
            clipboardLabel: "Your clipboard"
            // Words that stay on the clipboard never reach the app.
            sample: page._reachesApp ? "Can you send me last quarter's numbers by Friday?" : ""
            startLabel: "Your words"
            firstLabel: "First"
            fallbackLabel: "If that fails"
            keptLabel: "Also"
        }
    }

    Section {
        title: Schema.row("insertThroughAccessibility").section
        caption: "Speecher tries these in order. Turn off any you don't want; one always stays on."
        DeliveryStepRow { rowId: "insertThroughAccessibility" }
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
    }

    Section {
        title: Schema.row("reviewSelectionEdits").section
        SchemaRow { rowId: "reviewSelectionEdits" }
    }

    Section {
        title: Schema.row("categoryPasteRule_terminal").section
        caption: "Narrow the steps above for every app of one type. A rule can't turn on a step that's off."
        AccessibilityNotice { text: "Turn on desktop accessibility so Speecher can tell which app you're in. Rules you set apply once it can." }
        // Two columns of rows, so seven types don't make a tall stack. The
        // rows go across in pairs, so each pair's dividers line up. Where a
        // column would leave a type's name and examples less than 120 px
        // beside its choice, as at the window's narrowest, one column.
        Grid {
            id: typeGrid
            readonly property real narrowestColumn: Theme.rowInset * 2 + 120 + 20 + Theme.selectWidth
            columns: width >= narrowestColumn * 2 ? 2 : 1
            width: parent.width
            Repeater {
                model: Schema.appCategories
                SchemaRow {
                    id: typeRow
                    required property var modelData
                    required property int index
                    width: typeGrid.width / typeGrid.columns
                    first: index < typeGrid.columns
                    rowId: "categoryPasteRule_" + modelData.id
                    // A rule that relies on a step that's off says so.
                    readonly property string _needs: System.stepRuleNeeds(Settings.values[rowId] || "")
                    note: _needs ? Schema.pasting.needsStep(_needs) : modelData.examples
                }
            }
        }
    }

    AppPasteRules {}

    SubpageRows { pageId: "pasting" }
}
