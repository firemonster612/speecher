import QtQuick

// Placeholder: the app ships this mock until its wiring PR replaces it.
// A dictation as the popup shows it. The Dictation singleton is the live
// one; the Popup & sounds page makes its own for the preview. Core's
// DictationController and PopupPresentation would feed these.
QtObject {
    // idle, listening, paused, transcribing, refining, delivered, review, error
    property string state: "idle"
    property real level: 0             // input level, 0 to 1
    property int elapsed: 0            // seconds recorded
    property string words: ""          // what has been heard so far
    property string refined: ""        // cleaned-up text as it streams in
    property string speechService: "Claude Voice"
    property string refinementModel: "Claude Sonnet 5.5"
    property string targetApp: "Slack"
    property string profile: "Chat"

    // delivered: what happened, in one line and its detail.
    property var receipt: ({ title: "", detail: "" })
    // The receipt for this dictation once its text is in: where it went,
    // and who heard and cleaned it up.
    readonly property var deliveredReceipt: ({ title: "Pasted into " + targetApp,
                                               detail: "Heard by " + speechService + ", cleaned up by " + refinementModel })
    // review: the edit to a selection, as runs of unchanged, added and
    // removed text.
    property var review: ({ instruction: "", segments: [] })
    // error: what went wrong and the one action that fixes it.
    property var error: ({ title: "", detail: "", action: "" })

    // The popup's wording for this dictation.
    readonly property var text: ({
        // What the dictation is doing, as Home's heading says it.
        status: ({ listening: "Listening", paused: "Paused", transcribing: "Transcribing", refining: "Refining",
                   error: "Dictation failed" })[state] || "Ready to dictate",
        transcribing: "Transcribing…",
        refining: "Refining…",
        refiningWith: refinementModel + ", " + profile + " profile",
        reviewTitle: "Edit to your selection",
        youSaid: "You said “" + review.instruction + "”",
        askForMore: "Ask for more changes",
        replace: "Replace"
    })

    signal startRequested()
    signal stopRequested()
    signal pauseRequested()
    signal cancelRequested()
    signal replaceRequested()
    // review: dictate a further instruction for the same selection.
    signal followUpRequested()
    // error: the action error.action names, such as signing in again.
    signal errorActionRequested()
}
