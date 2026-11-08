import QtQuick
import Speecher.Theme
import Speecher.Controls
import Speecher.Backend
import Speecher.Wording

// The body of a sign-in row, under the cards that choose a service: where
// its sign-in comes from, as the select Accounts has, so it is changed where
// the problem shows. While it isn't ready the row says how to sign in or
// what went wrong in one plain sentence, with Check again and, behind
// Details, the failure in the words of the part that failed. Open Accounts
// is there in every state, and under the select, what Automatic is using
// once a check found it. The chosen card already says the state.
SettingRow {
    id: row

    required property string serviceId
    property bool ready: false
    property bool checking: false
    // The failure as the part that failed put it, or "".
    property string details
    signal recheck()
    readonly property var source: ModelText.signInSource(serviceId) || { rowId: "", options: [], choice: "", found: "" }

    stacked: !ready
    Column {
        spacing: Theme.space2
        Row {
            spacing: Theme.space3
            Select {
                objectName: "signInSource"
                visible: row.source.rowId !== ""
                placeholder: Words.choose
                options: row.source.options
                currentId: row.source.choice
                // Codex dictation shows the Codex app for every source but
                // CLI Proxy API; picking it again keeps the saved one.
                onChosen: id => { if (id !== row.source.choice) Settings.set(row.source.rowId, id) }
                anchors.verticalCenter: parent.verticalCenter
            }
            Button {
                visible: !row.ready
                text: Words.checkAgain
                size: "small"
                busy: row.checking
                onClicked: row.recheck()
                anchors.verticalCenter: parent.verticalCenter
            }
            AccountsLink { rowId: row.source.rowId; visible: rowId !== ""; anchors.verticalCenter: parent.verticalCenter }
        }
        Reveal {
            open: row.source.found !== ""
            Txt {
                objectName: "signInFound"
                width: parent.width
                text: row.source.found ? Schema.row(row.source.rowId).autoFound.arg(row.source.found) : ""
                variant: "small"
                tone: "secondary"
                wrapMode: Text.WordWrap
            }
        }
        ErrorDetails { text: row.ready ? "" : row.details }
    }
}
