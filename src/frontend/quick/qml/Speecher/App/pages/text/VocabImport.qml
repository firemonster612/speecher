import QtCore
import QtQuick
import QtQuick.Dialogs
import Speecher.Controls
import Speecher.Backend

// Importing a list from a file the desktop's chooser picks: terms from a
// CSV file, or replacements and snippets from a JSON file. What came in
// shows as a toast, as does why a file couldn't be read or parsed.
FileDialog {
    id: chooser

    // "terms" or "replacements"
    property string list
    readonly property bool terms: list === "terms"

    title: terms ? Library.vocabularyText.importTermsTitle : Library.vocabularyText.importReplacementsTitle
    currentFolder: StandardPaths.writableLocation(StandardPaths.HomeLocation)
    nameFilters: terms ? Library.vocabularyText.termsFiles : Library.vocabularyText.replacementsFiles
    onAccepted: {
        const result = terms ? Library.importTerms(selectedFile) : Library.importReplacements(selectedFile)
        if (result.error)
            Toaster.show(Library.importFailedText(result.error), { icon: "triangle-alert", tone: "negative" })
        else
            Toaster.show(Library.importedText(list, result.added), { icon: "file-up" })
    }
}
