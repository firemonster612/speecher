pragma ComponentBehavior: Bound
import QtCore
import QtQuick
import QtQuick.Dialogs
import Speecher.Theme
import Speecher.Controls
import Speecher.Backend
import Speecher.Wording
import Speecher.App
import "home"
import "home/words.js" as T

// Home: the latest dictation and whether Speecher is ready, then the chosen
// period's figures, the year's activity, and the lists that go with them in
// pairs across the page. With Insights off or nothing dictated yet, one
// line says so in their place. The words are home/words.js's.
Page {
    id: page
    // Whether the speech service is signed in, for what stands in the way.
    Component.onCompleted: Models.checkSignIns()

    readonly property var t: T.text
    readonly property var summary: Insights.summary

    // The share picture, drawn off screen only while it is copied.
    function copyImage() { shareImage.active = true }
    function shareFailed(title, detail) {
        share.confirm(title)
        Toaster.show(detail, { icon: "alert-triangle", tone: "negative" })
    }

    title: Schema.page("home").title
    headerActions: MenuButton {
        id: share
        objectName: "homeShare"
        opacity: Insights.hasData ? 1 : 0
        enabled: Insights.hasData
        Behavior on opacity { Ease {} }
        label: T.share.label
        leadingIcon: "share-2"
        items: [
            { id: "image", label: T.share.copyImage, icon: "image" },
            { id: "text", label: T.share.copyText, icon: "copy" },
            { id: "json", label: T.share.saveJson, icon: "file-json" }
        ]
        onPicked: id => {
            if (id === "image") return page.copyImage()
            if (id === "json") return jsonFile.ask()
            Insights.copyText(T.shareText(page.summary, Insights.period))
            confirm(Words.copied)
        }
    }

    // The desktop's own save dialog, which asks before saving over a file.
    FileDialog {
        id: jsonFile
        function ask() {
            jsonFile.selectedFile = jsonFile.currentFolder + "/" + T.jsonFileName(Insights.today)
            jsonFile.open()
        }
        title: T.share.saveTitle
        fileMode: FileDialog.SaveFile
        nameFilters: [T.share.jsonFilter]
        currentFolder: StandardPaths.writableLocation(StandardPaths.DocumentsLocation)
        onAccepted: {
            if (Insights.saveJson(jsonFile.selectedFile)) share.confirm(T.share.saved)
            else page.shareFailed(T.share.saveFailed, T.share.saveFailedDetail)
        }
    }

    // Left of the window, where nothing shows it: a grab needs a drawn item.
    Loader {
        id: shareImage
        parent: page.Window.contentItem
        readonly property ShareCard card: item as ShareCard
        x: -(card ? card.width : 0) - 100
        active: false
        sourceComponent: ShareCard {}
        function done(copied) {
            shareImage.active = false
            if (copied) share.confirm(Words.copied)
            else page.shareFailed(T.share.copyFailed, T.share.copyFailedDetail)
        }
        onLoaded: Qt.callLater(() => {
            const grabbing = card.grabToImage(result => shareImage.done(Insights.copyImage(result.image)),
                                              Qt.size(card.width * 2, card.height * 2))
            if (!grabbing) shareImage.done(false)
        })
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
                    options: T.periods
                    currentId: Insights.period
                    onActivated: id => Insights.period = id
                }
                Figures { rows: page.summary ? T.figures(page.summary, Insights.period, Insights.today) : [] }
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
                        values: page.summary ? page.summary.profiles.map(p => [T.profileName(p.name), Words.count(p.words, "word", "words"), p.percent + "%"]) : []
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
                        values: page.summary ? page.summary.apps.map(a => [T.appName(a), a.otherApps ? "" : T.profileName(a.profile), Words.count(a.words, "word", "words")]) : []
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
                        values: page.summary ? T.records(page.summary, Insights.today) : []
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
                    caption: page.summary ? T.milestoneCaption(page.summary) : ""
                    RowList {
                        id: milestoneList
                        values: page.summary ? T.milestones(page.summary) : []
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
                id: learned
                readonly property var action: T.correctionsAction(Insights.corrections)
                title: page.t.learnedCorrections
                // An empty card says it in the caption's place.
                caption: Insights.corrections.count ? T.correctionsCaption(Insights.corrections) : ""
                actions: Button {
                    objectName: "learnedCorrectionsAction"
                    text: learned.action.label
                    size: "small"
                    opacity: learned.action.label ? 1 : 0
                    enabled: learned.action.label !== ""
                    Behavior on opacity { Ease {} }
                    onClicked: Navigation.goToRow(learned.action.row)
                }
                Reveal {
                    width: parent.width
                    open: Insights.corrections.recent.length > 0
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
                    width: parent.width
                    open: Insights.corrections.recent.length === 0
                    EmptyState { width: parent.width; compact: true; iconName: "spell-check"; text: T.correctionsCaption(Insights.corrections) }
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
                    readonly property var tip: T.tip(Insights.today)
                    Txt { text: parent.tip.text; variant: "small"; tone: "secondary"; width: Math.min(implicitWidth, parent.width); wrapMode: Text.WordWrap }
                    TextLink { text: parent.tip.action; onActivated: Navigation.go(parent.tip.page) }
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
