pragma ComponentBehavior: Bound
import QtQuick
import Speecher.Theme
import Speecher.Controls
import Speecher.Backend
import Speecher.Wording
import Speecher.App

// The language you speak, then the speech services in the order Speecher
// tries them, each with its model and, after the first, its own language.
// How each signs in, the Local Model and the server are on Services; a
// service that can't run offers Fix in its row.
Page {
    id: page
    objectName: "speechPage"
    title: Schema.page("speech").title
    subtitle: Schema.page("speech").subtitle

    readonly property var order: ModelText.order("speech")
    readonly property var orderSpec: Schema.row("speechServices")

    Component.onCompleted: { Models.checkComputer(); ModelText.checkUnknownSignIns() }

    Section {
        // "Language and accuracy" only while the accuracy row shows.
        title: page.order.includes("codex") ? Schema.row("spokenLanguage").section : Schema.row("spokenLanguage").sectionAlone
        SchemaRow { rowId: "spokenLanguage" }
        Reveal {
            id: languageProblem
            readonly property string text: Models.spokenLanguageProblem
            open: text.length > 0
            Notice { inset: Theme.rowInset; text: languageProblem.text }
        }
        SchemaRow { rowId: "codexFinalRetranscribe"; shown: page.order.includes("codex") }
    }

    Section {
        id: servicesSection
        title: page.orderSpec.label
        caption: page.orderSpec.help
        SearchLanding { rowIds: ["speechServices"]; target: servicesSection }
        ServiceOrderList { role: "speech" }
    }

    // Vocabulary lives under Refinement, but its key terms help the
    // service hear names, so it is a click away from here.
    Section {
        SettingRow {
            title: Schema.page("vocabulary").title
            help: Library.speechVocabularyText
            Button { text: Library.openVocabulary; size: "small"; onClicked: Navigation.follow("vocabulary") }
        }
    }
}
