pragma Singleton
import QtQuick

// Placeholder: the app ships this mock until its wiring PR replaces it.
// Mock of the dictation in progress, which the popup shows, and the made-up
// one the Popup & sounds page plays in its preview.
DictationState {
    readonly property var sample: ({
        words: "Quick update on the launch, the onboarding flow is ready for review and I'd love feedback before Friday",
        receipt: { title: "Pasted into " + targetApp,
                   detail: Library.pipelineCaption(Settings.values.speechProvider, Settings.values.refinementProvider || "none") }
    })
    // A failed dictation for previews: an expired sign-in and its fix.
    readonly property var sampleError: ({ title: "Your Claude sign-in has expired", detail: "Sign in again to keep using Claude Voice.",
                                          action: "Sign in" })
}
