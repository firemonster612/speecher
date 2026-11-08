import QtQuick
import Speecher.Backend
import Speecher.App
import "../pages/speech"

// Speech to text's own sections: the language you speak, then the speech
// services in order, each with its status and a Fix while it can't run.
// It checks the sign-ins when it opens.
StepPage {
    Component.onCompleted: {
        Models.checkComputer()
        Models.checkSignIns()
    }

    SpokenLanguageSection {}
    ServiceOrderSection { role: "speech" }
}
