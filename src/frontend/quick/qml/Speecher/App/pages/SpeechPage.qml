pragma ComponentBehavior: Bound
import QtQuick
import Speecher.Controls
import Speecher.Backend
import Speecher.Wording
import Speecher.App
import "speech"

// The language you speak, then the speech services in the order Speecher
// tries them, each with its model and, after the first, its own language.
// How each signs in, the Local Model and the server are on Services; a
// service that can't run offers Fix in its row.
Page {
    id: page
    objectName: "speechPage"
    title: Schema.page("speech").title
    subtitle: Schema.page("speech").subtitle

    Component.onCompleted: { Models.checkComputer(); ModelText.checkUnknownSignIns() }

    SpokenLanguageSection {}
    ServiceOrderSection { role: "speech" }

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
