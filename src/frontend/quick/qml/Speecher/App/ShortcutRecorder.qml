import QtQuick
import Speecher.Controls
import Speecher.Backend
import Speecher.Wording

// A shortcut setting's ShortcutField, wired to Settings: clicking it records
// keys for rowId, with the bound shortcuts let go while it listens, or opens
// the desktop's chooser where the desktop picks the keys (the portal). The
// shortcut rows and Dictation's hero both use it.
ShortcutField {
    id: field
    required property string rowId

    emptyText: Words.notSet; listeningText: Words.pressShortcut; clearText: Words.clearShortcut
    // The keys that act while dictating may be Escape itself.
    escapeRecords: rowId === "cancelShortcut" || rowId === "pauseShortcut"
    keys: Settings.values[rowId] || []
    recordsKeys: !Settings.desktopPicksKeys(rowId)
    onClicked: if (!recordsKeys) Settings.act(rowId)
    // Every way listening ends takes the bound shortcuts back.
    onListeningChanged: listening ? Settings.startRecordingShortcut(rowId) : Settings.stopRecordingShortcut(rowId)
    Component.onDestruction: if (listening) Settings.stopRecordingShortcut(rowId)
    keyAlone: () => Settings.lastKeyAlone()
    takesAlone: keys => Settings.takesKeyAlone(rowId, keys)
    onRecorded: keys => Settings.set(rowId, keys)
}
