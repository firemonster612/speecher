import QtQuick
import Speecher.Theme
import Speecher.Controls
import Speecher.Backend
import Speecher.Wording
import Speecher.App

// The language you speak, as Speech to text and its setup step show it: the
// Spoken Language, why the first service can't hear it, and while ChatGPT is
// in the order, its second pass for accuracy.
Section {
    id: section
    readonly property bool _codex: ModelText.order("speech").includes("codex")

    // "Language and accuracy" only while the accuracy row shows.
    title: _codex ? Schema.row("spokenLanguage").section : Schema.row("spokenLanguage").sectionAlone
    SchemaRow { rowId: "spokenLanguage" }
    Reveal {
        id: languageProblem
        readonly property string text: Models.spokenLanguageProblem
        open: text.length > 0
        Notice { inset: Theme.rowInset; text: languageProblem.text }
    }
    SchemaRow { rowId: "codexFinalRetranscribe"; shown: section._codex }
}
