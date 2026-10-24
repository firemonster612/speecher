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
    title: Schema.page("updates").title
    subtitle: Schema.page("updates").subtitle

    readonly property var _channel: Schema.row("updateChannel").options.find(o => o.id === Settings.values.updateChannel)
    // A nightly's commits, then the releases' notes, each one Section.
    // Fixed for the run, so nothing comes or goes while the page is open.
    readonly property var _news: {
        const commits = Updates.whatsNew.commits || []
        const builds = commits.length ? [{ version: "", sections: [{ title: page.t.commits, items: commits }] }] : []
        return builds.concat(Updates.whatsNew.releases || [])
    }
    // Settings added since the version run before the update.
    readonly property var _newSettings: Object.keys(Schema.introduced).filter(id => Updates.newSince(Schema.introduced[id]))

    readonly property var t: ({
        current: v => "Speecher " + v,
        channel: label => label + " channel.",
        whatsNewIn: v => v ? "What's new in " + v : "What's new in this build",
        release: v => "Speecher " + v,
        commits: "Commits",
        allReleases: "All releases",
        newSettings: "New settings",
        newSettingsCaption: "Added since the version you used before.",
        noAutomaticDownloads: "This copy of Speecher can't install updates itself. Get new versions from the release page."
    })

    // Opening the page is seeing the news.
    Component.onCompleted: Updates.acknowledgeWhatsNew()

    Section {
        title: page.t.current(Status.version)
        caption: page._channel ? page.t.channel(page._channel.label) : ""
        UpdateStatus { channelLabel: page._channel ? page._channel.label : "" }
    }

    Section {
        title: Schema.row("updateChannel").section
        SchemaRow { rowId: "updateChannel"; note: page._channel ? page._channel.help : "" }
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
        // Only a copy that can replace itself installs in the background.
        SchemaRow {
            rowId: "autoInstallUpdates"
            enabled: Settings.saves(rowId) && Updates.automaticDownloads
            note: Updates.automaticDownloads ? spec.help : page.t.noAutomaticDownloads
        }
    }

    Repeater {
        model: page._news
        Section {
            id: news
            required property var modelData
            required property int index
            title: index === 0 ? page.t.whatsNewIn(modelData.version) : page.t.release(modelData.version)
            actions: [ Button { visible: news.index === 0; text: page.t.allReleases; variant: "ghost"; size: "small"; trailingIcon: "external-link"; onClicked: Updates.openAllReleases() } ]
            ReleaseNotes { groups: news.modelData.sections }
        }
    }

    // Settings added since the version you used before, each a way to it.
    Reveal {
        open: page._newSettings.length > 0
        Section {
            title: page.t.newSettings
            caption: page.t.newSettingsCaption
            RowList {
                id: newSettings
                values: page._newSettings
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
