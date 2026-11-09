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
    // A nightly's commits and their comparison, then the releases' notes,
    // each one Section. Fixed for the run, so nothing comes or goes while the
    // page is open.
    readonly property var _news: {
        const news = Updates.whatsNew
        const lines = (news.commits || []).map(page.t.commit)
        if (news.comparison) lines.push(page.t.compareCommits(news.comparison))
        const builds = lines.length ? [{ version: "", sections: [{ title: page.t.commits, items: lines }] }] : []
        return builds.concat(news.releases || [])
    }
    // The newest build's first few notes show; the rest of them, and the
    // releases before it, wait behind What's new, which opens by itself when
    // the page opens for an update's news.
    readonly property int _previewLines: 3
    readonly property var _latest: _news.length ? _news[0].sections : []
    readonly property var _preview: _latest.length ? [Object.assign({}, _latest[0], { items: _latest[0].items.slice(0, _previewLines) })] : []
    readonly property var _rest: {
        if (!_latest.length) return []
        const more = _latest[0].items.slice(_previewLines)
        return (more.length ? [{ title: "", items: more }] : []).concat(_latest.slice(1))
    }
    readonly property bool _hasMore: _rest.length > 0 || _news.length > 1
    property bool _newsOpen: false

    // Settings added since the version run before the update.
    readonly property var _newSettings: Object.keys(Schema.since).filter(id => Updates.newSince(Schema.since[id]))

    readonly property var t: ({
        current: v => "Speecher " + v,
        build: v => "Build " + v + ".",
        channel: label => label + " channel.",
        nightlyOnStable: "A nightly build. The next update comes from the Stable channel.",
        whatsNewIn: v => v ? "What's new in " + v : "What's new in this build",
        release: v => "Speecher " + v,
        commits: "Commits",
        // A line of Markdown: the title, then its pull request or commit as a link.
        commit: c => c.title + " ([" + (c.pullRequest ? "#" + c.pullRequest : c.sha) + "](" + c.url + "))",
        compareCommits: url => "[Compare commits](" + url + ")",
        allReleases: "All releases",
        showAll: "Show all of what's new",
        showLess: "Show less",
        newSettings: "New settings",
        newSettingsCaption: "Added since the version you used before.",
        releasePage: "release page"
    })

    // Opening the page is seeing the news.
    Component.onCompleted: {
        _newsOpen = Updates.whatsNewPending
        Updates.acknowledgeWhatsNew()
    }

    Section {
        // The number and channel; the whole build, for a bug report, is in
        // the caption.
        title: page.t.current(Words.versionNumber(Updates.version))
        // A nightly running on the Stable channel says the next update leaves it.
        caption: [!page._channel ? ""
                  : Settings.values.updateChannel === "stable" && Updates.version.indexOf("nightly") >= 0 ? page.t.nightlyOnStable
                  : page.t.channel(page._channel.label), page.t.build(Updates.version)].filter(s => s).join(" ")
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
            helpLink: Updates.automaticDownloads ? "" : page.t.releasePage
            onHelpLinkActivated: Updates.openAllReleases()
        }
    }

    Reveal {
        open: page._news.length > 0
        Section {
            title: page.t.whatsNewIn(page._news.length ? page._news[0].version : "")
            actions: [ Button { text: page.t.allReleases; variant: "ghost"; size: "small"; trailingIcon: "external-link"; onClicked: Updates.openAllReleases() } ]
            ReleaseNotes { groups: page._preview }
            Reveal {
                open: page._newsOpen
                ReleaseNotes { groups: page._rest }
            }
            SettingRow {
                shown: page._hasMore
                title: page._newsOpen ? page.t.showLess : page.t.showAll
                clickable: true
                trailingIcon: "chevron-down"
                trailingRotation: page._newsOpen ? 180 : 0
                onClicked: page._newsOpen = !page._newsOpen
            }
        }
    }

    // The releases the update crossed, under the newest.
    Reveal {
        open: page._newsOpen && page._news.length > 1
        Column {
            width: parent.width
            spacing: Theme.sectionGap
            Repeater {
                model: page._news.slice(1)
                Section {
                    id: news
                    required property var modelData
                    title: page.t.release(modelData.version)
                    ReleaseNotes { groups: news.modelData.sections }
                }
            }
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
                    onClicked: Navigation.follow(spec.page, key)
                }
            }
        }
    }
}
