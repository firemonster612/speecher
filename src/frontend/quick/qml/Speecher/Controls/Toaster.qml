pragma Singleton
import QtQuick

// Where anything in the app posts a toast: a short note about something
// that just happened away from where the person is looking, such as an
// export finishing. The window's Toast shows one at a time; a new one
// replaces the one showing. Say what happened in a few words; do not toast
// what the screen already shows.
//   Toaster.show("Exported 3 files", { icon: "check", actionText: "Show", action: () => … })
QtObject {
    signal posted(var toast)

    // options: { icon?, tone? (positive, warning, negative), actionText?, action? }
    function show(text, options) {
        posted(Object.assign({ text: text, icon: "", tone: "neutral", actionText: "", action: null }, options || {}))
    }
}
