import QtQuick
import Speecher.Controls
import Speecher.Backend
import Speecher.Wording

// Importing a list from a file named by its path, in a form at the top of
// the list's card: terms from a CSV file, or replacements and snippets from
// a JSON file. What came in shows as a toast; a file that can't be read or
// parsed keeps the form open, with why in a toast.
InlineForm {
    id: form

    // "terms" or "replacements"
    property string list
    readonly property bool terms: list === "terms"
    readonly property var t: Library.vocabularyText
    function start() { path.text = ""; open = true }

    title: terms ? t.importTermsTitle : t.importReplacementsTitle
    saveText: t.importAction
    cancelText: Words.cancel
    canSave: path.text.trim().length > 0
    onCancelled: open = false
    onSaved: {
        const result = terms ? Library.importTerms(path.text) : Library.importReplacements(path.text)
        if (result.error) {
            Toaster.show(Library.importFailedText(result.error), { icon: "triangle-alert", tone: "negative" })
            return
        }
        open = false
        Toaster.show(Library.importedText(list, result.added), { icon: "file-up" })
    }

    FormField {
        label: form.t.importFile
        help: form.terms ? form.t.importTermsHelp : form.t.importReplacementsHelp
        TextField {
            id: path
            width: parent.width
            iconName: "file-up"
            placeholderText: form.terms ? form.t.importTermsPlaceholder : form.t.importReplacementsPlaceholder
        }
    }
}
