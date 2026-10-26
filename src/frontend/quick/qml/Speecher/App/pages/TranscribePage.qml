pragma ComponentBehavior: Bound
import QtQuick
import Speecher.Controls
import Speecher.Backend
import Speecher.Wording
import Speecher.App
import "transcribe"

// Transcribe files: add audio and set the batch's options, watch it run
// with the transcript appearing live, then copy or export the results. The
// steps along the top say where the batch is, the view under them slides
// to the next step, and the one button in the header moves the batch on.
Page {
    id: page

    readonly property int step: Transcription.step
    // The cards say whether each service is signed in and a Local Runner
    // is running. Refinement providers' sign-ins come from Accounts'
    // checks, which start the first time anything asks for Accounts.
    Component.onCompleted: { ModelText.checkUnknownSignIns(); Models.detectRunners(); Accounts.services }

    title: Schema.page("transcribe").title
    subtitle: Schema.page("transcribe").subtitle
    headerActions: Button {
        text: page.step === 0 ? TranscribeWords.startCaption(Transcription.files.length) : page.step === 1 ? Words.cancel : TranscribeWords.transcribeMore
        iconName: page.step === 0 ? "audio-lines" : page.step === 1 ? "" : "plus"
        variant: page.step === 1 ? "danger" : "primary"
        enabled: page.step !== 0 || Transcription.files.length > 0
        onClicked: page.step === 0 ? Transcription.start() : page.step === 1 ? Transcription.cancel() : Transcription.reset()
    }

    StepIndicator {
        width: parent.width
        steps: TranscribeWords.steps
        current: page.step
    }

    Switcher {
        currentIndex: page.step
        ConfigureView {}
        RunningView {}
        ResultsView {}
    }
}
