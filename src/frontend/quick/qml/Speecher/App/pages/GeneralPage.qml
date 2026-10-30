pragma ComponentBehavior: Bound
import QtQuick
import Speecher.Theme
import Speecher.Controls
import Speecher.Backend
import Speecher.Wording
import Speecher.App
import "app"

Page {
    id: page
    objectName: "generalPage"
    title: Schema.page("general").title
    // Startup shows only where Speecher can start at login.
    subtitle: System.canLaunchAtLogin ? Schema.page("general").subtitle : Schema.page("general").subtitleNoStartup

    property bool _phoneOpen: false
    property bool _confirmClear: false
    property bool _confirmRemove: false
    property bool _removeSettings: false
    readonly property bool _removed: System.removal.steps !== undefined

    readonly property var t: ({
        showCode: "Show code",
        hideCode: "Hide code",
        remove: "Remove…",
        confirmRemove: "Remove Speecher",
        removeQuestion: "Remove Speecher from this computer?",
        removeDetail: "Dictation stops working. Any system helper Speecher installed goes too, and each asks for your password. The helpers are shared by every account on this computer. Your settings stay unless you delete them as well.",
        removeSettings: "Also delete my settings, vocabulary and API keys"
    })
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
        if (open) System.startPhoneTransfer()
        else System.stopPhoneTransfer()
    }
    Connections {
        target: page.Window.window
        function onVisibleChanged() { if (!page.Window.window.visible) page.showPhoneCode(false) }
    }
    Component.onDestruction: System.stopPhoneTransfer()

    SubpageRows { pageId: "general" }

    // Only where Speecher can start at login.
    Reveal {
        open: System.canLaunchAtLogin
        Section {
            title: Schema.row("launchAtLogin").section
            SchemaRow { rowId: "launchAtLogin" }
        }
    }

    // Two short groups side by side.
    Row {
        id: pair
        width: parent.width
        spacing: Theme.space4
        readonly property real _half: (width - spacing) / 2

        // The pair lines up: the same header and card heights.
        Section {
            id: phoneSection
            width: pair._half
            minimumHeaderHeight: insightsSection.naturalHeaderHeight
            minimumCardHeight: insightsSection.naturalCardHeight
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
                        text: System.phoneLink
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
                            text: System.phoneBundle.terms === undefined ? "" : page.phoneSummary(System.phoneBundle)
                            variant: "small"
                            wrapMode: Text.WordWrap
                        }
                        Txt {
                            width: parent.width
                            text: page.phoneNote(System.phoneTransfer)
                            variant: "small"
                            tone: "secondary"
                            wrapMode: Text.WordWrap
                        }
                    }
                }
            }
        }

        Section {
            id: insightsSection
            width: pair._half
            minimumHeaderHeight: phoneSection.naturalHeaderHeight
            minimumCardHeight: phoneSection.naturalCardHeight
            title: Schema.row("insightsEnabled").section
            SchemaRow {
                rowId: "insightsEnabled"
                help: Settings.values.insightsEnabled ? spec.help : spec.offHelp
            }
            SchemaRow {
                rowId: "clearInsights"
                help: Insights.kept > 0
                      ? Words.count(Insights.kept, "dictation", "dictations") + " kept since " + Qt.formatDate(Insights.firstDictation, "MMMM yyyy") + "."
                      : "No dictation history yet."
                Button {
                    text: "Clear history…"
                    variant: "danger"
                    size: "small"
                    enabled: Insights.kept > 0 && !page._confirmClear
                    onClicked: page._confirmClear = true
                }
            }
            InlineConfirm {
                cancelText: Words.cancel
                open: page._confirmClear
                text: "Delete " + Words.count(Insights.kept, "dictation", "dictations") + "?"
                detail: Words.cantUndo
                confirmText: Words.delete_
                onCancelled: page._confirmClear = false
                onConfirmed: {
                    page._confirmClear = false
                    if (!Insights.clear())
                        Toaster.show("Speecher couldn't delete the insights history. Close any app that has the history file open, then try again.",
                                     { icon: "alert-triangle", tone: "negative" })
                }
            }
        }
    }

    Section {
        visible: System.canRemove
        title: Schema.row("removeSpeecher").section
        SchemaRow {
            rowId: "removeSpeecher"
            Button {
                text: page.t.remove
                variant: "danger"
                size: "small"
                busy: System.removing
                enabled: !System.removing && !page._removed && !page._confirmRemove
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
                System.uninstall(!page._removeSettings)
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
}
