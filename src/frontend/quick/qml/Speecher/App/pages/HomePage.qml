pragma ComponentBehavior: Bound
import QtQuick
import Speecher.Theme
import Speecher.Controls
import Speecher.Backend
import Speecher.Wording
import Speecher.App
import "home"

// Home: the latest dictation and whether Speecher is ready, then the chosen
// period's figures, the year's activity, and the lists that go with them in
// pairs across the page. With Insights off or nothing dictated yet, one
// line says so in their place.
Page {
    id: page

    readonly property var t: Insights.text
    readonly property var summary: Insights.summary

    title: Schema.page("home").title
    headerActions: MenuButton {
        opacity: Insights.hasData ? 1 : 0
        enabled: Insights.hasData
        Behavior on opacity { Ease {} }
        label: Insights.share.label
        leadingIcon: "share-2"
        items: [
            { id: "image", label: Insights.share.copyImage, icon: "image" },
            { id: "text", label: Insights.share.copyText, icon: "copy" },
            { id: "json", label: Insights.share.saveJson, icon: "file-json" }
        ]
        onPicked: id => {
            if (id === "image") Insights.copyImage()
            if (id === "text") Insights.copyText()
            confirm(id === "json" ? Insights.saveJson() : Words.copied)
        }
    }

    SessionSection {}

    // Insights off, or nothing dictated yet.
    Reveal {
        open: !Insights.hasData
        Section {
            title: page.t.yourDictation
            SettingRow {
                title: Insights.state === "off" ? page.t.offTitle : page.t.emptyTitle
                help: Insights.state === "off" ? page.t.offBody : page.t.emptyBody
                Button { text: page.t.insightsSettings; size: "small"; onClicked: Navigation.go("general") }
            }
        }
    }

    Reveal {
        open: Insights.hasData
        Column {
            width: parent.width
            spacing: Theme.sectionGap
            Section {
                title: page.t.yourDictation
                actions: SegmentedControl {
                    objectName: "homePeriod"
                    options: Insights.periods
                    currentId: Insights.period
                    onActivated: id => Insights.period = id
                }
                Figures { rows: page.summary ? page.summary.rows : [] }
            }

            ActivitySection {}

            Row {
                width: parent.width
                spacing: Theme.space4
                Section {
                    id: profiles
                    width: (parent.width - parent.spacing) / 2
                    minimumHeaderHeight: apps.naturalHeaderHeight
                    minimumCardHeight: apps.naturalCardHeight
                    title: page.t.profilesUsed
                    RuledTable {
                        columns: [{}, { width: 110, align: "right", tone: "secondary" }, { width: 40, align: "right", variant: "label" }]
                        values: page.summary ? page.summary.profiles.map(p => [p.name, p.words, p.percent]) : []
                    }
                }
                Section {
                    id: apps
                    width: (parent.width - parent.spacing) / 2
                    minimumHeaderHeight: profiles.naturalHeaderHeight
                    minimumCardHeight: profiles.naturalCardHeight
                    title: page.t.apps
                    RuledTable {
                        columns: [{}, { width: 76, tone: "secondary" }, { width: 104, align: "right", tone: "secondary" }]
                        values: page.summary ? page.summary.apps.map(a => [a.name, a.profile, a.words]) : []
                    }
                }
            }

            Row {
                width: parent.width
                spacing: Theme.space4
                Section {
                    id: records
                    width: (parent.width - parent.spacing) / 2
                    minimumHeaderHeight: milestones.naturalHeaderHeight
                    minimumCardHeight: milestones.naturalCardHeight
                    title: page.t.records
                    RowList {
                        id: recordList
                        values: Insights.records
                        keyOf: r => r.title
                        delegate: ListRow {
                            id: recordRow
                            required property string key
                            required property int index
                            readonly property var record: recordList.entry(key)
                            width: ListView.view.width
                            first: index === 0 && recordList.first
                            title: record.title
                            detail: record.detail
                            trailing: MorphText { text: recordRow.record.value; variant: "label" }
                        }
                    }
                }
                Section {
                    id: milestones
                    width: (parent.width - parent.spacing) / 2
                    minimumHeaderHeight: records.naturalHeaderHeight
                    minimumCardHeight: records.naturalCardHeight
                    title: page.t.milestones
                    caption: Insights.milestoneCaption
                    RowList {
                        id: milestoneList
                        values: Insights.milestones
                        keyOf: m => m.label
                        delegate: ListRow {
                            id: milestone
                            required property string key
                            required property int index
                            readonly property var entry: milestoneList.entry(key)
                            readonly property bool next: entry.state === "next"
                            width: ListView.view.width
                            first: index === 0 && milestoneList.first
                            title: entry.label
                            opacity: entry.state === "later" ? 0.45 : 1
                            Behavior on opacity { Ease {} }
                            trailing: Row {
                                spacing: milestone.next ? Theme.space3 : 0
                                ProgressBar {
                                    width: milestone.next ? 96 : 0
                                    opacity: milestone.next ? 1 : 0
                                    value: milestone.entry.progress
                                    anchors.verticalCenter: parent.verticalCenter
                                    Behavior on width { Ease { duration: Theme.slow } }
                                    Behavior on opacity { Ease {} }
                                }
                                MorphText {
                                    text: milestone.entry.status
                                    variant: "small"
                                    tone: "secondary"
                                    anchors.verticalCenter: parent.verticalCenter
                                }
                            }
                        }
                    }
                }
            }

            // What dictation learned from your edits: heard above, fixed below.
            Section {
                title: page.t.learnedCorrections
                caption: Insights.corrections.caption
                actions: Button {
                    text: page.t.reviewCorrections
                    size: "small"
                    onClicked: Navigation.go("vocabulary")
                }
                Row {
                    id: corrections
                    x: Theme.rowInset
                    width: parent.width - 2 * Theme.rowInset
                    Repeater {
                        model: Insights.corrections.recent
                        Item {
                            id: correction
                            required property var modelData
                            required property int index
                            width: corrections.width / Insights.corrections.recent.length
                            height: pair.height + 2 * Theme.space3
                            Rectangle { visible: correction.index > 0; y: Theme.space3; width: 1; height: pair.height; color: Theme.rule }
                            Column {
                                id: pair
                                x: correction.index > 0 ? Theme.space4 : 0
                                y: Theme.space3
                                width: parent.width - x - Theme.space4
                                spacing: 2
                                Txt { width: parent.width; text: correction.modelData.heard; tone: "tertiary"; font.strikeout: true; elide: Text.ElideRight }
                                Txt { width: parent.width; text: correction.modelData.fixed; tone: "accent"; variant: "label"; elide: Text.ElideRight }
                            }
                        }
                    }
                }
            }

            Reveal {
                open: Insights.recent.length > 1
                Section {
                    title: page.t.recent
                    RowList {
                        id: recentList
                        values: Insights.recent.slice(1)
                        keyOf: d => d.text
                        delegate: ListRow {
                            id: dictation
                            required property string key
                            required property int index
                            width: ListView.view.width
                            first: index === 0 && recentList.first
                            title: key
                            titleVariant: "body"
                            wrapTitle: true
                            detail: Insights.recentDetail(recentList.entry(key))
                            trailing: CopyButton {
                                tooltip: page.t.copy
                                copiedText: Words.copied
                                onCopied: Insights.copyRecent(dictation.index + 1)
                            }
                        }
                    }
                }
            }

            // A tip and where Insights live, in a quiet voice under the page.
            Column {
                x: 2
                width: parent.width - 4
                spacing: Theme.space1
                Flow {
                    width: parent.width
                    spacing: Theme.space2
                    Txt { text: Insights.tip.text; variant: "small"; tone: "secondary"; width: Math.min(implicitWidth, parent.width); wrapMode: Text.WordWrap }
                    TextLink { text: Insights.tip.action; onActivated: Navigation.go(Insights.tip.page) }
                }
                Flow {
                    width: parent.width
                    spacing: Theme.space2
                    Txt { text: page.t.privacyNote; variant: "small"; tone: "tertiary" }
                    TextLink { text: page.t.insightsSettings; onActivated: Navigation.go("general") }
                }
            }
        }
    }
}
