import QtQuick
import Speecher.Backend
import Speecher.Controls
import Speecher.App
import "../pages/refinement"

// Refinement's own sections: the sample, Clean up my words, and while it is
// on the services that clean up, in order, each with its status and a Fix.
// Each service's model, thinking and speed wait behind a link: a first run
// needs only the services.
StepPage {
    RefinementSample {}
    Section {
        SchemaRow { rowId: "refinementEnabled" }
    }
    Reveal {
        open: Settings.values.refinementEnabled !== false
        ServiceOrderSection { role: "refinement"; brief: true }
    }

    // Refinement services' sign-ins come from Accounts' checks, which
    // start the first time anything asks for Accounts.
    Component.onCompleted: { Models.checkComputer(); ModelText.checkUnknownSignIns(); Accounts.services }
}
