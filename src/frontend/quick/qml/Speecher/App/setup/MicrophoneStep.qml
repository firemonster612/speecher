import QtQuick
import Speecher.Backend
import "../pages/speech"

// The Microphone page's test and input card. The test runs while the step
// is on show, so speaking is all the step asks.
StepPage {
    MicCheck {
        id: check
        Component.onCompleted: Audio.holdTest(check)
    }
    MicrophoneInput {}
}
