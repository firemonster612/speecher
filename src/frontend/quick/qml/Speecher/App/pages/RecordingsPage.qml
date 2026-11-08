pragma ComponentBehavior: Bound
import QtCore
import QtQuick
import QtQuick.Dialogs
import Speecher.Theme
import Speecher.Controls
import Speecher.Backend
import Speecher.Wording
import Speecher.App
import "recordings"
import "history"
import "recordings/RecordingWords.js" as T

// Recordings: the recorder, then while one runs its transcript as it is
// written, then the settings a recording runs with, then the latest
// recordings, which are History's entries in History's own rows.
Page {
    id: page
    title: Schema.page("recordings").title
    subtitle: Schema.page("recordings").subtitle

    readonly property var t: T.text
    readonly property bool running: Recording.state === "recording" || Recording.state === "stopping"
    // The newest recordings, read again as History changes.
    readonly property var recent: { void History.kept; return History.recent("recording", 5) }
    // The first speech service of the Service Order that can record, which
    // the speech service row's "Same as" names.
    readonly property string pageService: ModelText.order("speech").find(id => id !== "local" && ModelText.streams(id)) || ""

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
        open: page.running
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

    Section {
        title: Schema.row("recordingSources").section
        SchemaRow {
            rowId: "recordingSources"
            // Where this computer can't capture what it plays, the row
            // stays at Microphone only and says why.
            enabled: Recording.canRecordCallAudio
            value: Recording.canRecordCallAudio ? Settings.values.recordingSources : "mic"
            note: Recording.canRecordCallAudio ? spec.help : spec.fixedHelp
        }
        SettingRow {
            id: serviceRow
            readonly property var spec: Schema.row("recordingService")
            title: spec.label
            help: spec.help
            SearchLanding { rowIds: ["recordingService"]; target: serviceRow; onLanded: serviceRow.highlight() }
            InheritSelect {
                inheritedText: Words.sameAs(page.t.sameAsSpeech,
                                            page.pageService ? Models.service(page.pageService).label : page.t.noneCanRecord)
                choices: Models.speechServices.filter(s => s.id !== "local" && ModelText.streams(s.id))
                                              .map(s => ({ id: s.id, label: s.label }))
                currentId: (Settings.values.recordingService || {}).service || ""
                onChosen: id => Settings.set("recordingService", id ? { service: id, model: "" } : {})
            }
        }
        SettingRow {
            id: folderRow
            readonly property var spec: Schema.row("recordingFolder")
            title: spec.label
            help: T.shortPath(Recording.plan.folder, StandardPaths.writableLocation(StandardPaths.HomeLocation).toString().replace("file://", ""))
            SearchLanding { rowIds: ["recordingFolder"]; target: folderRow; onLanded: folderRow.highlight() }
            Row {
                spacing: Theme.space2
                Button { text: folderRow.spec.actionLabel; size: "small"; onClicked: folderDialog.open() }
                Button { text: folderRow.spec.openLabel; size: "small"; trailingIcon: "external-link"; onClicked: Qt.openUrlExternally("file://" + Recording.plan.folder) }
            }
            FolderDialog {
                id: folderDialog
                title: folderRow.spec.dialogTitle
                currentFolder: "file://" + Recording.plan.folder
                onAccepted: Settings.set("recordingFolder", selectedFolder.toString().replace("file://", ""))
            }
        }
        SchemaRow { rowId: "recordingFormat" }
        SchemaRow { rowId: "recordingKeepAudio" }
        SchemaRow { rowId: "recordingAutoStop" }
        SchemaRow { rowId: "recordingShortcut" }
        SchemaRow { rowId: "recordingPill" }
    }

    Section {
        id: recentSection
        title: Schema.row("recentRecordings").section
        actions: Button {
            text: page.t.allInHistory
            size: "small"
            onClicked: Navigation.follow("history", "")
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
