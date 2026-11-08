pragma ComponentBehavior: Bound
import QtCore
import QtQuick
import QtQuick.Dialogs
import Speecher.Theme
import Speecher.Controls
import Speecher.Backend
import "RunWords.js" as T

// Where Transcribe files and Recordings save their transcripts, in one
// shape: where, when the page offers more than a folder; the folder, with
// Change and Open; and the file format, when the page offers a choice. The
// page's own rows follow.
Section {
    id: root

    // [{ id, label, icon }] of where they can go, "folder" being the folder
    // below; empty for the folder alone.
    property var destinations: []
    property string destination: "folder"
    property string destinationHelp
    // The folder as shown, and as a URL.
    property string folder
    property url folderUrl
    property string folderHelp
    // [{ id, label }], or empty for no choice.
    property var formats: []
    property string format
    property string formatHelp
    // Rows search lands on, by id; "" for none.
    property string folderRowId
    property string formatRowId
    signal destinationChosen(string id)
    // The folder chooser's answer, an empty URL when it was dismissed.
    signal folderChosen(url folder)
    signal formatChosen(string id)

    function chooseFolder() { chooser.open() }

    title: T.text.saveTranscripts

    SettingRow {
        shown: root.destinations.length > 0
        title: T.text.where
        help: root.destinationHelp
        Select {
            options: root.destinations
            currentId: root.destination
            onChosen: id => root.destinationChosen(id)
        }
    }
    SettingRow {
        id: folderRow
        shown: root.destination === "folder"
        title: T.text.folder
        help: root.folderHelp || T.shortPath(root.folder, StandardPaths.writableLocation(StandardPaths.HomeLocation).toString().replace("file://", ""))
        SearchLanding { rowIds: root.folderRowId ? [root.folderRowId] : []; target: folderRow; onLanded: folderRow.highlight() }
        Row {
            spacing: Theme.space2
            Button { text: T.text.change; size: "small"; onClicked: chooser.open() }
            Button {
                visible: root.folder.length > 0
                text: T.text.open
                size: "small"
                trailingIcon: "external-link"
                onClicked: Qt.openUrlExternally(root.folderUrl)
            }
        }
    }
    SettingRow {
        id: formatRow
        shown: root.formats.length > 0
        title: T.text.format
        help: root.formatHelp
        SearchLanding { rowIds: root.formatRowId ? [root.formatRowId] : []; target: formatRow; onLanded: formatRow.highlight() }
        SegmentedControl {
            options: root.formats
            currentId: root.format
            onActivated: id => root.formatChosen(id)
        }
    }

    FolderDialog {
        id: chooser
        title: T.text.folder
        currentFolder: root.folder ? root.folderUrl : StandardPaths.writableLocation(StandardPaths.HomeLocation)
        onAccepted: root.folderChosen(selectedFolder)
        onRejected: root.folderChosen("")
    }
}
