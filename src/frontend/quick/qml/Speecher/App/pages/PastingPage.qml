pragma ComponentBehavior: Bound
import QtQuick
import Speecher.Theme
import Speecher.Controls
import Speecher.Backend
import Speecher.Wording
import Speecher.App
import "text"

// How words get into apps: the delivery steps in the order core tries
// them, each with what it relies on here; then the options, and a read-only
// list of the Apps and App Types whose Pasting choice narrows the steps,
// each opening in Apps.
Page {
    id: page
    title: Schema.page("pasting").title
    subtitle: Schema.page("pasting").subtitle

    readonly property var _steps: System.deliverySteps
    function _runs(id) { return _steps.some(s => s.id === id && s.on && s.ready) }
    // The steps that are on, in words, each marked where this computer
    // can't run it yet: [{ icon, label, ready }].
    readonly property var _route: _steps.filter(s => s.on).map(s => Object.assign({ id: s.id, ready: s.ready }, ({
        insert: { icon: "text-cursor-input", label: "Insert" },
        press: { icon: "keyboard", label: "Copy, press " + Schema.pasting.pasteKeys },
        keep: { icon: "clipboard", label: "Keep on clipboard" }
    })[s.id]))
    // Where the words end up (System.pastingOutcome): the words reach the
    // app, or stay on a clipboard Keep can use.
    readonly property bool _reachesApp: System.pastingOutcome === "delivers"
    readonly property bool _keeps: _runs("keep")
    readonly property string _routeCaption: {
        // What the steps that are on but can't run yet wait for, by name.
        const waiting = [...new Set([].concat(..._steps.filter(s => s.on).map(s => s.waiting))
                                      .map(n => Schema.pasting.needs[n].label))].join(" or ")
        if (!_reachesApp && _keeps)
            return waiting ? "Your words stay on the clipboard until " + waiting + " is ready."
                           : "Your words stay on the clipboard for you to paste."
        if (!_reachesApp)
            return "Nothing delivers your words until " + waiting + " is ready."
        // Insertion writes plain text whatever the format, and so does a
        // paste through wl-copy, without Speecher's clipboard helper.
        const formatted = Settings.values.outputFormat === "html"
        const delivers = !formatted || !_runs("press") ? "Delivers plain text"
                       : !System.copiesFormatted ? "Delivers plain text until Speecher's clipboard helper is available"
                       : _runs("insert") ? "Plain text when inserted, formatted when pasted" : "Delivers formatted text"
        if (_keeps) return delivers + ", and leaves a copy on your clipboard."
        if (!_runs("press")) return delivers + ", and leaves your clipboard alone."
        return delivers + ". After a paste your clipboard goes back, unless Speecher can't confirm the paste, which leaves your words there."
    }
    // Why a selection edit can't be reviewed now, or "": reading the
    // selection needs accessibility (Windows' needs no permission), then
    // applying an edit needs a step that can replace it, which on Linux,
    // where insertion can't, is Press.
    readonly property string _selectionLimit: {
        const reads = Qt.platform.os === "windows" || System.accessibility
        const applies = Qt.platform.os === "linux" ? _runs("press") : _reachesApp
        return [reads ? "" : "Reading the selection needs " + Schema.pasting.needs.accessibility.label
                             + (Qt.platform.os === "linux" ? ", which Insert above turns on." : "."),
                applies ? "" : Qt.platform.os === "linux" ? "Applying an edit needs Press the paste keys, on and ready."
                                                          : "Applying an edit needs Insert or Press to run."]
            .filter(s => s).join(" ")
    }

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

    DeliverySteps {}

    Section {
        title: Schema.row("outputFormat").section
        SchemaRow { rowId: "outputFormat" }
        SchemaRow {
            rowId: "reviewSelectionEdits"
            // A saved preference this computer can't act on says why.
            note: spec.help + (Settings.values.reviewSelectionEdits && page._selectionLimit ? " " + page._selectionLimit : "")
        }
    }

    Section {
        id: differently
        objectName: "appsPasteDifferently"
        // What each narrows the steps to here, App Types first.
        readonly property var entries: Apps.types.filter(o => o.pasting).map(o => ({
            id: "apptype:" + o.id, name: Schema.apps.type(o.id).label, typeIcon: Schema.apps.type(o.id).icon, icon: "",
            method: o.pasting, terminalWins: o.id === "ai_coding" && Apps.types.some(o => o.id === "terminal" && o.pasting)
        })).concat(Apps.entries.filter(e => e.pasting && e.pasting !== "standard_paste").map(e => ({
            id: "appentry:" + e.id, name: e.name, typeIcon: "", icon: e.icon, method: e.pasting, terminalWins: false
        })))
        readonly property var _check: Health.find(System.checks, "desktopAccessibility")
        title: Schema.row("appsPasteDifferently").label
        caption: Schema.row("appsPasteDifferently").help
        actions: [ Button { text: "Change in Apps"; size: "small"; onClicked: Navigation.follow("apps", "appTypes") } ]
        SearchLanding { rowIds: ["appsPasteDifferently"]; target: differently }

        // Insert's row above turns accessibility on, so this only says why.
        AccessibilityNotice {
            open: differently.entries.length > 0 && (differently._check.reason === "off" || differently._check.reason === "unsupported")
            offersFix: false
            text: Schema.pasting.appsNotice(System.identifiesAppsWithoutAccessibility)
        }
        RowList {
            id: differentList
            values: differently.entries
            delegate: ListRow {
                id: differentRow
                required property string key
                required property int index
                readonly property var entry: differentList.entry(key)
                objectName: "differently_" + key
                width: ListView.view.width
                first: index === 0 && differentList.first
                implicitHeight: Math.max(Theme.rowHeight, words.implicitHeight + 22)
                clickable: true
                Accessible.name: entry.name
                onClicked: Navigation.follow("apps", key)
                AppIcon {
                    id: icon
                    anchors.verticalCenter: parent.verticalCenter
                    name: differentRow.entry.typeIcon ? "" : differentRow.entry.name
                    iconName: differentRow.entry.typeIcon
                    source: differentRow.entry.icon ? "image://appicon/" + encodeURIComponent(differentRow.entry.icon) : ""
                    size: 28
                }
                Column {
                    id: words
                    x: icon.width + Theme.space3
                    width: parent.width - x
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 3
                    Txt { width: parent.width; text: differentRow.entry.name; variant: "label"; elide: Text.ElideRight }
                    Txt {
                        objectName: "differentlyResult"
                        width: parent.width
                        // The choice, then what it does here, unless that
                        // already says it ("Clipboard only").
                        text: {
                            const method = differentRow.entry.method
                            const label = Schema.apps.pastingLabel(method)
                            const result = Schema.pasting.ruleResult(System.rulePlan(method), method, differentRow.entry.terminalWins)
                            return result.startsWith(label) ? result : label + ": " + result
                        }
                        variant: "small"
                        tone: "secondary"
                        wrapMode: Text.WordWrap
                    }
                }
                trailing: [ Icon { anchors.verticalCenter: parent.verticalCenter; name: "chevron-right"; size: 16 } ]
            }
        }
        Reveal {
            open: differently.entries.length === 0
            EmptyState { compact: true; title: "Every app follows the steps above"; text: "Give an app or app type its own way of pasting in Apps." }
        }
    }

    SubpageRows { pageId: "pasting" }
}
