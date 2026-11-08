import QtCore
import QtQuick
import QtQuick.Dialogs
import Speecher.Controls
import Speecher.Backend
import Speecher.Wording

// Importing into the Vocabulary from a file the desktop's chooser picks:
// spellings from a CSV file, or replacements and snippets from a JSON file.
// What came in shows as a toast, as does why a file couldn't be read or
// parsed.
FileDialog {
    title: Schema.vocabulary.t.importTitle
    currentFolder: StandardPaths.writableLocation(StandardPaths.HomeLocation)
    nameFilters: Schema.vocabulary.t.importFiles
    onAccepted: {
        const result = Library.importVocabulary(selectedFile)
        if (result.error)
            Toaster.show(Schema.vocabulary.importFailedText(result.error), { icon: "triangle-alert", tone: "negative" })
        else
            Toaster.show(Schema.vocabulary.importedText(result.added), { icon: "file-up" })
    }
}
