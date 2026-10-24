import QtQuick

// Mock of the C++ DictationState (host/QuickDictation.h), which documents
// each field: a dictation as the popup shows it. The preview's Dictation is
// one, played by the preview; pages make their own for a demo.
QtObject {
    // idle, starting, listening, paused, transcribing, refining, delivering,
    // delivered, review or error
    property string state: "idle"
    property real level: 0
    property int elapsed: 0
    property string words: ""
    property string refined: ""
    // { title, detail, outcome: inserted|copied|fallback|cancelled, page }
    property var receipt: ({ title: "", detail: "", outcome: "inserted", page: "" })
    // { instruction, whole, folded, summary, keepKeys, replaceKeys, followUpKeys, following }
    property var review: ({ instruction: "", whole: [], folded: [], summary: "", keepKeys: [], replaceKeys: [],
                            followUpKeys: [], following: false })
    // { title, detail, page }
    property var error: ({ title: "", detail: "", page: "" })

    signal startRequested()
    signal stopRequested()
    signal pauseRequested()
    signal cancelRequested()
    signal replaceRequested()
    signal followUpRequested()
    signal errorActionRequested()
}
