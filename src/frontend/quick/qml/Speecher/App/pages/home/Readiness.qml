pragma ComponentBehavior: Bound
import QtQuick
import Speecher.Theme
import Speecher.Controls
import Speecher.Backend
import Speecher.Wording
import Speecher.App
import "words.js" as T

// Home's readiness, at the top of the session card: one line while
// everything passes; otherwise every check with its fix, in two groups,
// what dictation needs and what it works without. Run setup again either
// way. A check's row is its HealthRow, the one its own page shows; rows that
// are not System.checks (the microphone, the speech service, pasting) come
// as items. Each row opens the setting it is about.
Column {
    id: root

    required property bool allPass
    // Check ids, from System.checks.
    required property var neededChecks
    required property var optionalChecks
    // [{ id, title, status, state, page, row, help?, fixLabel?, fixCheck? }]:
    // state is a States id, which tones the status; row is the schema row to
    // land on, or empty for the page; fixLabel and fixCheck the button that
    // runs a System check's fix.
    required property var neededItems
    required property var optionalItems
    readonly property var t: T.text

    width: parent ? parent.width : implicitWidth

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
            clickable: true
            onClicked: Navigation.goToRow(checkRow.spec.page, checkRow.spec.opens || checkRow.key)
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
            onClicked: itemRow.item.row ? Navigation.goToRow(itemRow.item.page, itemRow.item.row) : Navigation.go(itemRow.item.page)
            Row {
                spacing: Theme.space3
                StateText { text: itemRow.item.status; stateId: itemRow.item.state; anchors.verticalCenter: parent.verticalCenter }
                Button {
                    visible: !!itemRow.item.fixLabel
                    text: itemRow.item.fixLabel || ""
                    size: "small"
                    anchors.verticalCenter: parent.verticalCenter
                    onClicked: System.fix(itemRow.item.fixCheck)
                }
            }
        }
    }

    Reveal {
        open: root.allPass
        SettingRow {
            first: true
            title: root.t.allWorking
            Button { text: root.t.runSetupAgain; size: "small"; onClicked: Settings.act("runSetup") }
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
                title: root.t.setupAssistant
                help: root.t.setupAgainHelp
                Button { text: root.t.runSetupAgain; size: "small"; onClicked: Settings.act("runSetup") }
            }
        }
    }
}
