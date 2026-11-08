pragma ComponentBehavior: Bound
import QtQuick
import Speecher.Theme
import Speecher.Controls
import Speecher.Backend
import Speecher.Wording
import Speecher.App
import "apps"
import "text"

// How words get into apps: the delivery steps in the order core tries
// them, each with what it relies on here; then the options, and a read-only
// list of the Apps and App Types with steps or paste keys of their own, each
// opening in Apps.
Page {
    id: page
    title: Schema.page("pasting").title
    subtitle: Schema.page("pasting").subtitle

    readonly property var _steps: System.deliverySteps
    function _runs(id) { return _steps.some(s => s.id === id && s.on && s.ready) }
    // The steps that are on, in words, each marked where this computer
    // can't run it yet: [{ icon, label, ready }].
    readonly property var _route: _steps.filter(s => s.on).map(s => ({
        id: s.id, ready: s.ready, icon: Schema.row(Schema.pasting.rowOf[s.id]).icon,
        label: Schema.pasting.stepLabels[s.id] + (s.id === "press" ? " (" + Schema.pasting.pasteKeys + ")" : "")
    }))
    // Where the words end up (System.pastingOutcome): the words reach the
    // app, or stay on a clipboard Keep can use.
    readonly property bool _reachesApp: System.pastingOutcome === "delivers"
    readonly property bool _keeps: _runs("keep")
    readonly property string _routeCaption: {
        // What the steps that are on but can't run yet wait for, by name.
        const waiting = Schema.pasting.waitingNeeds(_steps).map(n => Schema.pasting.needs[n].label).join(" or ")
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
        return delivers + ". After a paste your clipboard goes back as it was."
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
                applies ? "" : Qt.platform.os === "linux" ? "Applying an edit needs Press paste keys, on and ready."
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
        // Those with steps or keys of their own, App Types first, with what
        // they do here.
        readonly property var entries: Apps.types.filter(o => o.steps || o.keys).map(o => Object.assign(Schema.apps.typeLink(o), {
            effective: o.effective,
            terminalWins: Schema.pasting.pastesLikeTerminals(o.id, Apps.types)
        })).concat(Apps.entries.filter(e => e.steps || e.keys).map(e => Object.assign(Schema.apps.appLink(e), {
            effective: e.effective, terminalWins: false
        })))
        title: Schema.row("appsPasteDifferently").label
        caption: Schema.row("appsPasteDifferently").help
        actions: [ Button { objectName: "changeInApps"; text: "Change in Apps"; size: "small"; onClicked: Navigation.follow("apps", "appTypes") } ]
        SearchLanding { rowIds: ["appsPasteDifferently"]; target: differently }

        RowList {
            id: differentList
            values: differently.entries
            delegate: AppLinkRow {
                required property string key
                required property int index
                objectName: "differently_" + key
                first: index === 0 && differentList.first
                entry: differentList.entry(key)
                // What its steps and keys do here; Apps lists what it sets.
                detail: Schema.pasting.pastingResult(System.stepsPlan(entry.effective.steps), entry.effective.keys,
                                                     entry.terminalWins)
            }
        }
        Reveal {
            open: differently.entries.length === 0
            EmptyState { compact: true; title: "Every app follows the steps above"; text: "Give an app or app type its own steps or paste keys in Apps." }
        }
    }

    SubpageRows { pageId: "pasting" }
}
