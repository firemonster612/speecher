pragma Singleton
import QtQuick
import Speecher.Backend

// Whether a settings row can be used now, by the condition its schema entry
// names in usableWhen: one rule its page and search both read. A row that
// can't be used shows off and greyed with its disabledHelp, and its saved
// value comes back when it can.
QtObject {
    // The conditions rows name, each true while this computer and the
    // settings allow it. Asked only for the row that names one, so a window
    // reads only the singletons its rows need.
    readonly property var _conditions: ({
        blur: () => Appearance.canBlur,
        accessibility: () => System.accessibility,
        automaticDownloads: () => Updates.automaticDownloads,
        // A refinement provider a dictation may use reads screenshots.
        screenshotReader: () => ModelText.refinersInUse().some(id => !!ModelText.provider(id).readsScreenshots)
    })

    function usable(spec) { return !spec.usableWhen || !!_conditions[spec.usableWhen]() }
}
