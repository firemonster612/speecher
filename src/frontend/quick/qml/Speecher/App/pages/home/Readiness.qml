pragma ComponentBehavior: Bound
import QtQuick
import Speecher.Theme
import Speecher.Controls
import Speecher.Backend
import Speecher.Wording
import Speecher.App
import "words.js" as T

// Whether Speecher is ready to dictate, as Home's session card and setup's
// Ready step show it: one line while everything passes; otherwise every
// check with its fix, in two groups, what dictation needs and what it works
// without. A check's row is its HealthRow, the one its own page shows; the
// microphone, the speech service and pasting, which are not System.checks,
// come as items. Each row opens the setting it is about, and a speech
// service that can't run has a Fix that opens its Services card. Everything
// reads Settings, System and Models as they show the settings, so setup's
// draft shows here too.
Column {
    id: root
    objectName: "readiness"

    // In setup: no Run setup again, and a link leads back to the assistant.
    property bool inSetup: false
    // Opens what a row is about, a page and the row on it to land on.
    property var openRow: (pageId, rowId) => Navigation.follow(pageId, rowId, inSetup ? "setup" : "")

    readonly property var t: T.text

    // Every check that applies here, by id, in two groups: what dictation
    // can't start without (a check core marks needed) and what it works
    // without. The virtual keyboard is Pasting's, and so is accessibility
    // while a delivery step that is on relies on it; microphone access is
    // the Microphone row's.
    readonly property var steps: System.deliverySteps
    readonly property bool pastingNeedsAccessibility: steps.some(s => s.on && s.needs.includes("accessibility"))
    readonly property var checks: System.checks.filter(c => Health.applies(c) && c.id !== "virtualKeyboardHealth"
                                                             && c.id !== "microphoneAccess"
                                                             && (c.id !== "desktopAccessibility" || !pastingNeedsAccessibility))
    readonly property var neededChecks: checks.filter(c => c.needed).map(c => c.id)
    readonly property var optionalChecks: checks.filter(c => !c.needed).map(c => c.id)

    // The speech services in order: dictation works while any of them does.
    // Checking while none works and one is being checked; a service nobody
    // has checked yet isn't ready, though dictation can start to try it, and
    // one whose check failed is missing.
    readonly property var chain: ModelText.order("speech").map(id => ModelText.service(id))
    readonly property var working: chain.find(s => s.state === "ready")
    readonly property string speechState: working ? "ok"
        : chain.some(s => s.signInChecking) ? "checking"
        : chain.some(s => s.state === "unknown" && !s.signInCheckFailure) ? "notChecked" : "attention"
    // The service the row is about: the one working, else the first that
    // isn't checked yet, else the first.
    readonly property var speechService: working || chain.find(s => s.state === "unknown") || chain[0] || null
    // While no service works and the first's problem is a sign-in, a model,
    // a runner or a server, the row opens its card on Services and its Fix
    // opens that card over the page.
    readonly property bool speechFixable: !working && !!speechService && ModelText.fixable(speechService)
                                          && ModelText.cardOf(speechService.id) !== ""
    readonly property var speechItem: ({
        id: "speech", title: Schema.page("speech").title, state: speechState,
        page: speechFixable ? "services" : "speech", row: speechFixable ? ModelText.servicesRow("speech", speechService.id) : "",
        status: working ? (working === chain[0] ? ModelText.status(working) : t.takesOver(working.label))
              : speechState === "checking" ? ModelText.checking
              : ModelText.status(speechService),
        fixLabel: speechFixable ? Words.fix : "", fixService: speechFixable ? speechService.id : ""
    })
    // The microphone, one row: access first, with its fix, then whether
    // the one chosen is there.
    readonly property var access: Health.find(System.checks, "microphoneAccess")
    readonly property bool accessFails: !Health.passes(access)
    readonly property var microphoneItem: ({
        id: "microphone", title: Schema.page("microphone").title, page: "microphone", row: accessFails ? "microphoneAccess" : "audioDevice",
        state: accessFails ? access.state : !Audio.hasMicrophones || Microphones.deviceMissing ? "missing" : "ok",
        status: accessFails ? Health.status(access)
              : !Audio.hasMicrophones ? Schema.row("audioDevice").noneLabel
              : Microphones.deviceMissing ? Schema.row("audioDevice").missingLabel : Microphones.deviceName,
        help: accessFails ? Health.failure(access) || Health.detail(access) : "",
        fixLabel: accessFails ? Health.fix(access) : "",
        fixCheck: "microphoneAccess"
    })
    // Pasting, one row while a step that is on waits for something: where
    // the words end up, what is missing by name, and the first fix.
    readonly property var waiting: [...new Set([].concat(...steps.filter(s => s.on).map(s => s.waiting)))]
    readonly property var fixFor: waiting.map(n => Schema.pasting.needs[n])
        .find(need => need.check && Health.fix(Health.find(System.checks, need.check))) || null
    readonly property var pastingItem: ({
        id: "pasting", title: Schema.page("pasting").title, page: "pasting", row: "",
        state: System.pastingOutcome === "nothing" ? "off" : "attention",
        status: Schema.pasting.outcomes[System.pastingOutcome],
        help: [t.needs(waiting.map(n => Schema.pasting.needs[n].label))]
            .concat(waiting.map(n => Schema.pasting.needs[n].repair || "")).filter(s => s).join(" "),
        fixLabel: fixFor ? fixFor.fix || Health.fix(Health.find(System.checks, fixFor.check)) : "",
        fixCheck: fixFor ? fixFor.check : ""
    })
    readonly property bool pastingDegraded: waiting.length > 0 || System.pastingOutcome !== "delivers"
    // With nothing to deliver the words, dictation is for nothing.
    readonly property bool pastingNeeded: System.pastingOutcome === "nothing"
    readonly property var neededItems: [microphoneItem, speechItem].concat(pastingDegraded && pastingNeeded ? [pastingItem] : [])
    readonly property var optionalItems: pastingDegraded && !pastingNeeded ? [pastingItem] : []

    // What dictation needs and doesn't have, as a caption names it, from the
    // same results as the rows.
    readonly property var missing: checks.filter(c => c.needed && !Health.passes(c)).map(c => Schema.row(c.id).label.toLowerCase())
        .concat(accessFails ? [Schema.row("microphoneAccess").label.toLowerCase()] : [],
                !Audio.hasMicrophones ? [t.aMicrophone] : Microphones.deviceMissing ? [t.yourMicrophone] : [],
                speechState === "attention" ? [t.aSpeechService] : [],
                pastingNeeded ? [t.aWayToDeliver] : [])
    // Without microphone access or a microphone nothing can start; with no
    // working speech service it can, to try again, but isn't ready.
    readonly property bool cantStart: accessFails || checks.some(c => c.needed && !Health.passes(c)) || !Audio.hasMicrophones
    readonly property bool blocked: missing.length > 0
    readonly property bool allPass: !blocked && checks.every(Health.passes) && microphoneItem.state === "ok"
                                    && speechState === "ok" && !pastingDegraded

    width: parent ? parent.width : implicitWidth

    // Accessibility listed here relies on no delivery step that is on, so
    // what it does is tell apps apart; its row opens Apps.
    function checkTarget(spec) {
        return spec.id === "desktopAccessibility" ? ["apps", "apps"] : [spec.page, spec.opens || spec.id]
    }

    component GroupLabel: Txt {
        x: Theme.rowInset
        width: root.width - 2 * Theme.rowInset
        topPadding: Theme.space3
        variant: "label"
        tone: "secondary"
    }
    component Checks: RowList {
        delegate: HealthRow {
            id: checkRow
            objectName: "readiness_" + key
            required property string key
            required property int index
            first: checkRow.index === 0
            rowId: checkRow.key
            fromSetup: root.inSetup
            clickable: true
            onClicked: root.openRow(...root.checkTarget(checkRow.spec))
            AccessibilityMenu { offered: checkRow.key === "desktopAccessibility" }
        }
    }
    component Items: RowList {
        id: items
        delegate: SettingRow {
            id: itemRow
            objectName: "readiness_" + key
            required property string key
            required property int index
            readonly property var item: items.entry(key)
            first: itemRow.index === 0 && items.first
            title: itemRow.item.title
            help: itemRow.item.help || ""
            clickable: true
            onClicked: root.openRow(itemRow.item.page, itemRow.item.row || "")
            Row {
                spacing: Theme.space3
                StateText { text: itemRow.item.status; stateId: itemRow.item.state; anchors.verticalCenter: parent.verticalCenter }
                Button {
                    visible: !!itemRow.item.fixLabel
                    text: itemRow.item.fixLabel || ""
                    size: "small"
                    anchors.verticalCenter: parent.verticalCenter
                    onClicked: itemRow.item.fixService ? sheet.openFor("speech", itemRow.item.fixService)
                                                       : root.inSetup ? System.fixFromSetup(itemRow.item.fixCheck)
                                                       : System.fix(itemRow.item.fixCheck)
                }
            }
        }
    }

    Reveal {
        open: root.allPass
        SettingRow {
            first: true
            title: root.t.allWorking
            Button { visible: !root.inSetup; text: root.t.runSetupAgain; size: "small"; onClicked: Settings.act("runSetup") }
        }
    }
    Reveal {
        open: !root.allPass
        Column {
            width: parent.width
            GroupLabel { text: root.t.needsThese }
            Checks { values: root.neededChecks }
            Items { values: root.neededItems; first: root.neededChecks.length === 0 }
            Reveal {
                open: root.optionalChecks.length + root.optionalItems.length > 0
                Column {
                    width: parent.width
                    GroupLabel { text: root.t.worksWithout }
                    Checks { values: root.optionalChecks }
                    Items { values: root.optionalItems; first: root.optionalChecks.length === 0 }
                }
            }
            SettingRow {
                shown: !root.inSetup
                title: root.t.setupAssistant
                help: root.t.setupAgainHelp
                Button { text: root.t.runSetupAgain; size: "small"; onClicked: Settings.act("runSetup") }
            }
        }
    }

    FixSheet { id: sheet }
}
