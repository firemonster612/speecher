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

    readonly property var t: T.text
    readonly property bool running: Recording.state !== "off"
    // The newest recordings, read again as History changes.
    readonly property var recent: { void History.kept; return History.recent("recording", 5) }
    // The speech services that can record, and those that can't.
    readonly property var canRecord: Models.speechServices.filter(s => Recording.plan.canRecord.includes(s.id))
    readonly property var cannotRecord: Models.speechServices.filter(s => !canRecord.some(c => c.id === s.id))
    // The speech service row's "Same as", naming the service a recording
    // takes unless one is chosen.
    readonly property string inheritedServiceText: Words.sameAs(t.sameAsSpeech, Recording.plan.inheritedService
                                                       ? Models.service(Recording.plan.inheritedService).label : t.noneCanRecord)
    readonly property var chosenService: Settings.values.recordingService || {}
    readonly property string nextRecording: running ? T.text.nextRecording : ""
    // Call audio is recorded only where both sources are chosen and this
    // computer can capture them; the page's words follow.
    readonly property bool micOnly: Recording.plan.sources !== "both"
    findField: transcript.open ? liveLines.findField : null
    subtitle: micOnly ? page.t.subtitleMicOnly : Schema.page("recordings").subtitle

    // The service's status under Run with says whether it is signed in.
    Component.onCompleted: {
        ModelText.checkUnknownSignIns()
        Recording.lines.forEach(line => lines.append(line))
    }
    Connections {
        target: Recording
        function onLineAdded(line) { lines.append(line) }
        function onStateChanged() { if (Recording.state === "starting") lines.clear() }
    }
    ListModel { id: lines }

    Recorder { width: parent.width }

    // The transcript as it is written, following the newest line.
    Reveal {
        id: transcript
        open: Recording.state === "recording" || Recording.state === "stopping"
        Section {
            width: parent.width
            title: page.t.transcript
            actions: Button { text: page.t.openTranscript; iconName: "external-link"; size: "small"; onClicked: Qt.openUrlExternally("file://" + Recording.path) }
            TranscriptReader {
                id: liveLines
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
        // The fold holds only the speech service, which follows Speech to
        // text, not the profile.
        sameText: page.inheritedServiceText
        speechService: Recording.plan.service || ""
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
                inheritedText: page.inheritedServiceText
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
        SchemaRow {
            rowId: "recordingKeepAudio"
            note: page.micOnly ? spec.micOnlyHelp : spec.help
        }
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
        // Its help says what counts as speaking, with a link to where that
        // is set, which stays usable while this is on.
        SchemaRow {
            rowId: "recordingAutoStop"
            note: T.autoStopHelp(spec.help, Schema.valueLabel(Schema.row("vadThresholdPercent"), Settings.values.vadThresholdPercent))
            helpLink: Schema.row("vadThresholdPercent").label
            onHelpLinkActivated: Navigation.follow("microphone", "vadThresholdPercent")
        }
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
        RowList {
            id: recentList
            values: page.recent
            delegate: HistoryRow {
                required property string key
                required property int index
                entry: recentList.entry(key)
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
