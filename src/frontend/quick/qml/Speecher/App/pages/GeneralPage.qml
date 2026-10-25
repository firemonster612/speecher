pragma ComponentBehavior: Bound
import QtQuick
import Speecher.Theme
import Speecher.Controls
import Speecher.Backend
import Speecher.App
import "app"

Page {
    id: page
    objectName: "generalPage"
    title: Schema.page("general").title
    subtitle: Schema.page("general").subtitle

    property bool _phoneOpen: false
    property bool _confirmClear: false
    property bool _confirmRemove: false
    property bool _removeSettings: false

    Section {
        title: Schema.row("themeControl").section
        SchemaRow {
            rowId: "themeControl"
            ThemePicker {
                width: 408
                options: Schema.row("themeControl").options
                currentId: Settings.values.themeControl || "system"
                // The window applies it, following the desktop on System.
                onActivated: id => Settings.set("themeControl", id)
            }
        }
        SchemaRow { rowId: "launchAtLogin" }
    }

    Section {
        title: Schema.row("runSetup").section
        caption: System.checksText
        HealthRow { rowId: "microphoneAccess" }
        HealthRow { rowId: "desktopAccessibility" }
        HealthRow { rowId: "globalShortcuts" }
        HealthRow { rowId: "virtualKeyboardHealth" }
        SchemaRow { rowId: "runSetup" }
    }

    // Two short groups side by side.
    Row {
        id: pair
        width: parent.width
        spacing: Theme.space4
        readonly property real _half: (width - spacing) / 2

        Section {
            width: pair._half
            title: Schema.row("copyToPhone").section
            SchemaRow {
                rowId: "copyToPhone"
                stacked: true
                Button {
                    text: page._phoneOpen ? System.generalText.hideCode : System.generalText.showCode
                    iconName: page._phoneOpen ? "chevron-up" : "qr-code"
                    size: "small"
                    onClicked: page._phoneOpen = !page._phoneOpen
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
                        text: System.phoneLink
                    }
                    Column {
                        anchors.left: qr.right
                        anchors.leftMargin: Theme.space4
                        anchors.right: parent.right
                        anchors.rightMargin: Theme.rowInset
                        anchors.verticalCenter: qr.verticalCenter
                        spacing: Theme.space2
                        Txt { width: parent.width; text: System.phoneSummary; variant: "small"; wrapMode: Text.WordWrap }
                        Txt {
                            width: parent.width
                            text: System.phoneNote
                            variant: "small"
                            tone: "secondary"
                            wrapMode: Text.WordWrap
                        }
                    }
                }
            }
        }

        Section {
            width: pair._half
            title: Schema.row("insightsEnabled").section
            SchemaRow {
                rowId: "insightsEnabled"
                help: Settings.values.insightsEnabled ? spec.help : spec.offHelp
            }
            SchemaRow {
                rowId: "clearInsights"
                help: Insights.keptText
                Button {
                    text: Insights.clearActionText
                    variant: "danger"
                    size: "small"
                    enabled: Insights.kept > 0 && !page._confirmClear
                    onClicked: page._confirmClear = true
                }
            }
            InlineConfirm {
                cancelText: Words.cancel
                open: page._confirmClear
                confirmText: Insights.clearText
                onCancelled: page._confirmClear = false
                onConfirmed: { Insights.clear(); page._confirmClear = false }
            }
        }
    }

    Section {
        title: Schema.row("removeSpeecher").section
        SchemaRow {
            rowId: "removeSpeecher"
            Button {
                text: System.generalText.remove
                variant: "danger"
                size: "small"
                enabled: System.installed && !page._confirmRemove
                onClicked: page._confirmRemove = true
            }
        }
        InlineConfirm {
            cancelText: Words.cancel
            open: page._confirmRemove
            confirmText: System.generalText.confirmRemove
            onCancelled: page._confirmRemove = false
            onConfirmed: {
                Toaster.show(System.uninstall(!page._removeSettings))
                page._confirmRemove = false
            }
            leading: CheckBox {
                text: System.generalText.removeSettings
                checked: page._removeSettings
                onToggled: page._removeSettings = checked
            }
        }
    }
}
