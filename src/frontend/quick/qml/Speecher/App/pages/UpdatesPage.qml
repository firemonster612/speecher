pragma ComponentBehavior: Bound
import QtQuick
import Speecher.Theme
import Speecher.Controls
import Speecher.Backend
import Speecher.App
import "app"

Page {
    id: page
    title: Schema.page("updates").title
    subtitle: Schema.page("updates").subtitle

    readonly property var _channel: Schema.row("updateChannel").options.find(o => o.id === Settings.values.updateChannel)

    Section {
        title: Updates.currentTitle
        caption: Updates.releasedText(page._channel ? page._channel.label : "")
        UpdateStatus { channelLabel: page._channel ? page._channel.label : "" }
    }

    Section {
        title: Schema.row("updateChannel").section
        SchemaRow { rowId: "updateChannel"; help: page._channel ? page._channel.help : "" }
        SchemaRow { rowId: "autoCheckUpdates" }
        SchemaRow { rowId: "updateCheckInterval" }
        SchemaRow {
            id: customInterval
            objectName: "customIntervalRow"
            rowId: "updateCheckCustomInterval"
            shown: Settings.values.autoCheckUpdates && Settings.values.updateCheckInterval === "custom"
            Row {
                spacing: Theme.space2
                TextField {
                    implicitWidth: 64
                    horizontalAlignment: TextInput.AlignHCenter
                    text: customInterval.value.number
                    validator: IntValidator { bottom: 1; top: 999 }
                    onEditingFinished: Settings.set(customInterval.rowId, { number: Number(text), unit: customInterval.value.unit })
                }
                SegmentedControl {
                    options: customInterval.spec.options
                    currentId: customInterval.value.unit
                    anchors.verticalCenter: parent.verticalCenter
                    onActivated: id => Settings.set(customInterval.rowId, { number: customInterval.value.number, unit: id })
                }
            }
        }
        SchemaRow { rowId: "autoInstallUpdates" }
    }

    Section {
        title: Updates.whatsNewTitle
        actions: [ Button { text: Updates.text.allReleases; variant: "ghost"; size: "small"; trailingIcon: "external-link"; onClicked: Updates.openAllReleases() } ]
        ReleaseNotes { groups: Updates.releaseNotes }
    }

    // Settings added since the version you used before, each a way to it.
    Reveal {
        open: Updates.newSettings.length > 0
        Section {
            title: Updates.text.newSettings
            caption: Updates.text.newSettingsCaption
            RowList {
                id: newSettings
                values: Updates.newSettings
                delegate: SettingRow {
                    required property string key
                    required property int index
                    readonly property var spec: Schema.row(key)
                    first: index === 0 && newSettings.first
                    title: spec.label
                    help: spec.help || ""
                    clickable: true
                    trailingText: Schema.page(spec.page).title
                    onClicked: Navigation.goToRow(key)
                }
            }
        }
    }
}
