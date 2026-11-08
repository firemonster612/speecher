pragma ComponentBehavior: Bound
import QtQuick
import Speecher.Theme
import Speecher.Controls
import Speecher.Backend
import Speecher.Wording
import Speecher.App
import "app"
import "history"
import "history/HistoryWords.js" as HistoryText

Page {
    id: page
    objectName: "generalPage"
    title: Schema.page("general").title
    subtitle: Schema.page("general").subtitle

    property bool _phoneOpen: false
    // The level What Speecher Keeps is lowered to while it asks about the
    // text already kept.
    property string _lowerTo: ""
    property bool _confirmRemove: false
    property bool _removeSettings: false
    readonly property bool _removed: Removal.report.steps !== undefined

    readonly property var t: ({
        showCode: "Show code",
        hideCode: "Hide code",
        remove: "Remove…",
        confirmRemove: "Remove Speecher",
        removeQuestion: "Remove Speecher from this computer?",
        removeDetail: "Dictation stops working. Any system helper Speecher installed goes too, and each asks for your password. The helpers are shared by every account on this computer. Your settings stay unless you delete them as well.",
        removeSettings: "Also delete my settings, vocabulary and API keys",
        deleteKeptQuestion: "Delete the text you've kept?",
        deleteKeptDetail: n => Words.count(n, "dictation or file holds", "dictations and files hold") + " text. Keep it and it goes when its days are up.",
        keepKept: "Keep it"
    })
    // Keeping less than text asks about the text already kept.
    function chooseKeeps(id) {
        const below = id === "nothing" || id === "stats"
        if (below && History.kept > 0 && (Settings.values.historyKeeps === "text" || Settings.values.historyKeeps === "audio"))
            _lowerTo = id
        else
            Settings.set("historyKeeps", id)
    }
    // "Brings over 7 vocabulary terms, 4 snippets and 6 Writing Profiles."
    function phoneSummary(bundle) {
        return "Brings over " + Words.list([
            Words.count(bundle.terms, "vocabulary term", "vocabulary terms"),
            Words.count(bundle.replacements, "snippet", "snippets"),
            Words.count(bundle.profiles, "writing profile", "writing profiles")]) + ". Never your accounts or API keys."
    }
    function phoneNote(state) {
        switch (state) {
        case "sent": return "Sent. Finish the import on your phone."
        case "interrupted": return "Your phone lost the connection. Hide the code and show it again for a new one."
        case "noNetwork": return "This computer isn't on a network. Connect it to the same Wi-Fi as your phone, then show the code again."
        case "failed": return "Speecher couldn't open a network port for your phone."
        default: return "In Speecher for Android, go to Settings, then Import from computer. The code works once, while it shows, on the same network as " + System.computerName + "."
        }
    }
    // The code works only while it shows: hiding the window takes it away,
    // and showing it again makes a new one.
    function showPhoneCode(open) {
        _phoneOpen = open
        if (open) Phone.start()
        else Phone.stop()
    }
    Connections {
        target: page.Window.window
        function onVisibleChanged() { if (!page.Window.window.visible) page.showPhoneCode(false) }
    }
    Component.onDestruction: Phone.stop()

    // The switch where Speecher can start at login, else how to do it by hand.
    Section {
        title: Schema.row("launchAtLogin").section
        SchemaRow { rowId: "launchAtLogin" }
        SchemaRow { rowId: "launchAtLoginGuide" }
    }

    // What Speecher keeps of each dictation, for how long, and clearing it.
    Section {
        id: historySection
        title: Schema.row("historyKeeps").section
        SchemaRow {
            id: keepsRow
            rowId: "historyKeeps"
            stacked: true
            note: (spec.options.find(o => o.id === value) || { help: "" }).help + " " + spec.help
            SegmentedControl {
                objectName: "historyKeeps"
                options: keepsRow.spec.options
                currentId: keepsRow.value ?? ""
                onActivated: id => page.chooseKeeps(id)
            }
        }
        // Keeping less than text: what is kept goes now, or when its days are up.
        InlineConfirm {
            open: page._lowerTo !== ""
            text: page.t.deleteKeptQuestion
            detail: page.t.deleteKeptDetail(History.kept)
            confirmText: Words.delete_
            cancelText: page.t.keepKept
            onConfirmed: {
                Settings.set("historyKeeps", page._lowerTo)
                if (!History.clear("all", false))
                    Toaster.show(HistoryText.text.clearFailed, { icon: "alert-triangle", tone: "negative" })
                page._lowerTo = ""
            }
            onCancelled: {
                Settings.set("historyKeeps", page._lowerTo)
                page._lowerTo = ""
            }
        }
        SchemaRow { rowId: "historyTextDays" }
        SchemaRow { rowId: "historyAudioDays" }
        SchemaRow {
            rowId: "clearHistory"
            note: Insights.kept > 0
                  ? Words.count(Insights.kept, "dictation", "dictations") + " counted since " + Qt.formatDate(Insights.firstDictation, "MMMM yyyy") + ", "
                    + Words.count(History.kept, "with its text", "with their text") + "."
                  : spec.help
            MenuButton {
                objectName: "clearHistory"
                label: HistoryText.text.clear
                items: HistoryText.text.clearRanges
                onPicked: id => { if (!clearConfirm.ask(id)) confirm(HistoryText.text.nothingToClear) }
            }
        }
        ClearConfirm { id: clearConfirm }
    }

    Section {
        id: phoneSection
        title: Schema.row("copyToPhone").section
        SchemaRow {
            rowId: "copyToPhone"
            stacked: true
            Button {
                text: page._phoneOpen ? page.t.hideCode : page.t.showCode
                iconName: page._phoneOpen ? "chevron-up" : "qr-code"
                size: "small"
                onClicked: page.showPhoneCode(!page._phoneOpen)
            }
        }
        Reveal {
            open: page._phoneOpen
            Item {
                width: parent.width
                implicitHeight: qr.height + Theme.space4
                QrCode {
                    id: qr
                    x: Theme.rowInset
                    width: 148
                    height: 148
                    // Empty once sent, as the code works once.
                    text: Phone.link
                }
                Column {
                    anchors.left: qr.right
                    anchors.leftMargin: Theme.space4
                    anchors.right: parent.right
                    anchors.rightMargin: Theme.rowInset
                    anchors.verticalCenter: qr.verticalCenter
                    spacing: Theme.space2
                    // Empty until the first code is shown.
                    Txt {
                        width: parent.width
                        text: Phone.bundle.terms === undefined ? "" : page.phoneSummary(Phone.bundle)
                        variant: "small"
                        wrapMode: Text.WordWrap
                    }
                    Txt {
                        width: parent.width
                        text: page.phoneNote(Phone.state)
                        variant: "small"
                        tone: "secondary"
                        wrapMode: Text.WordWrap
                    }
                }
            }
        }
    }


    // One row, which needs no heading over it.
    Section { SchemaRow { rowId: "runSetup" } }

    // Removing Speecher where it can remove itself, else how the system does.
    Section {
        title: Schema.row("removeSpeecher").section
        SchemaRow { rowId: "removeSpeecherGuide" }
        SchemaRow {
            rowId: "removeSpeecher"
            Button {
                text: page.t.remove
                variant: "danger"
                size: "small"
                busy: Removal.running
                enabled: !Removal.running && !page._removed && !page._confirmRemove
                onClicked: page._confirmRemove = true
            }
        }
        InlineConfirm {
            cancelText: Words.cancel
            open: page._confirmRemove
            text: page.t.removeQuestion
            detail: page.t.removeDetail
            confirmText: page.t.confirmRemove
            onCancelled: page._confirmRemove = false
            onConfirmed: {
                Removal.start(!page._removeSettings)
                page._confirmRemove = false
            }
            leading: CheckBox {
                text: page.t.removeSettings
                checked: page._removeSettings
                onToggled: page._removeSettings = checked
            }
        }
        Reveal {
            open: page._removed
            RemovalReport { inset: Theme.rowInset }
        }
    }

    SubpageRows { pageId: "general" }
}
