pragma ComponentBehavior: Bound
import QtQuick
import Speecher.Theme
import Speecher.Controls
import Speecher.Backend
import Speecher.Wording
import Speecher.App
import "recordings"
import "history"
import "transcripts"
import "recordings/RecordingWords.js" as T

// Recordings: the recorder, then while one runs its transcript as it is
// written, then what a recording runs with and where it saves, in the same
// shape as Transcribe files, then how it records, then the latest
// recordings, which are History's entries in History's own rows. While one
// runs, the settings are for the next.
Page {
    id: page
    title: Schema.page("recordings").title
    subtitle: Schema.page("recordings").subtitle

    readonly property var t: T.text
    readonly property bool running: Recording.state !== "off"
    // The newest recordings, read again as History changes.
    readonly property var recent: { void History.kept; return History.recent("recording", 5) }
    // The speech services that can record, the first of the Service Order's
    // of them, which the speech service row's "Same as" names, and those
    // that can't.
    readonly property var canRecord: Models.speechServices.filter(s => s.id !== "local" && ModelText.streams(s.id))
    readonly property var cannotRecord: Models.speechServices.filter(s => !canRecord.includes(s))
    readonly property string pageService: ModelText.order("speech").find(id => id !== "local" && ModelText.streams(id)) || ""
    readonly property var chosenService: Settings.values.recordingService || {}
    readonly property string nextRecording: running ? T.text.nextRecording : ""

    Component.onCompleted: Recording.lines.forEach(line => lines.append(line))
    Connections {
        target: Recording
        function onLineAdded(line) { lines.append(line) }
        function onStateChanged() { if (Recording.state === "starting") lines.clear() }
    }
    ListModel { id: lines }

    Recorder { width: parent.width }

    // The transcript as it is written, following the newest line.
    Reveal {
        open: Recording.state === "recording" || Recording.state === "stopping"
        Section {
            width: parent.width
            title: page.t.transcript
            actions: Button { text: page.t.openFile; iconName: "external-link"; size: "small"; onClicked: Qt.openUrlExternally("file://" + Recording.path) }
            TranscriptReader {
                topPadding: Theme.space3
                bottomPadding: Theme.space3
                model: lines
                live: true
                maxHeight: 320
            }
        }
    }

    RunWith {
        id: runWith
        caption: page.nextRecording
        profileId: Settings.values.recordingProfile || ""
        inheritedProfile: Recording.plan.inheritedProfile || ""
        profileHelp: Schema.row("recordingProfile").help
        changesTitle: page.t.changes
        changesHelp: page.t.changesHelp
        changes: page.chosenService.service ? 1 : 0
        // Its status shows beside Record instead.
        onProfileChosen: id => Settings.set("recordingProfile", id)

        SettingRow {
            id: serviceRow
            title: Schema.row("recordingService").label
            help: T.serviceHelp(page.canRecord.map(s => s.label), page.cannotRecord.map(s => s.label))
            SearchLanding {
                rowIds: ["recordingService"]
                target: serviceRow
                onLanded: { runWith.openChanges(); serviceRow.highlight() }
            }
            InheritSelect {
                inheritedText: Words.sameAs(page.t.sameAsSpeech,
                                            page.pageService ? Models.service(page.pageService).label : page.t.noneCanRecord)
                choices: page.canRecord.map(s => ({ id: s.id, label: s.label }))
                currentId: page.chosenService.service || ""
                onChosen: id => Settings.set("recordingService", id ? { service: id, model: "" } : {})
            }
        }
    }

    SearchLanding { rowIds: ["recordingProfile"]; target: runWith }

    SaveTranscripts {
        caption: page.nextRecording
        folder: Recording.plan.folder
        folderUrl: Recording.plan.folderUrl
        folderRowId: "recordingFolder"
        formats: Schema.row("recordingFormat").options
        format: Settings.values.recordingFormat
        formatHelp: Schema.row("recordingFormat").help
        formatRowId: "recordingFormat"
        onFolderChosen: folder => Recording.setFolder(folder)
        onFormatChosen: id => Settings.set("recordingFormat", id)
        SchemaRow { rowId: "recordingKeepAudio" }
    }

    Section {
        title: Schema.row("recordingSources").section
        caption: page.nextRecording
        SchemaRow {
            rowId: "recordingSources"
            // Where this computer can't capture what it plays, the row
            // stays at Microphone only and says why.
            enabled: Recording.canRecordCallAudio
            value: Recording.canRecordCallAudio ? Settings.values.recordingSources : "mic"
            note: Recording.canRecordCallAudio ? spec.help : spec.fixedHelp
        }
        SchemaRow { rowId: "recordingAutoStop" }
        // The shortcut is set on Dictation with the others; this says
        // which it is.
        SettingRow {
            id: shortcutLine
            readonly property var spec: Schema.row("recordingShortcut")
            readonly property var keys: Settings.values.recordingShortcut || []
            shown: !!System.hasGlobalShortcuts
            title: spec.label
            help: keys.length ? spec.setOn : spec.notSet
            Row {
                spacing: Theme.space3
                KeyCombo { visible: shortcutLine.keys.length > 0; keys: shortcutLine.keys; anchors.verticalCenter: parent.verticalCenter }
                Button { text: shortcutLine.spec.change; size: "small"; onClicked: Navigation.follow("dictation", "recordingShortcut") }
            }
        }
    }

    Section {
        id: recentSection
        title: Schema.row("recentRecordings").section
        actions: Button {
            text: page.t.allInHistory
            size: "small"
            onClicked: Navigation.follow("history", "historyKind:recording")
        }
        SearchLanding { rowIds: ["recentRecordings"]; target: recentSection }
        Repeater {
            model: page.recent
            HistoryRow {
                required property var modelData
                required property int index
                entry: modelData
                first: index === 0
                // The one running is on this page already.
                onReadRequested: { if (!entry.live) Navigation.follow("history", "recording:" + entry.id) }
                onDeleteRequested: Navigation.follow("history", "recording:" + entry.id)
            }
        }
        Reveal {
            open: page.recent.length === 0
            EmptyState { width: parent.width; compact: true; text: page.t.noRecordings }
        }
    }
}
