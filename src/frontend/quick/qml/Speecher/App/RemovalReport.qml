import QtQuick
import Speecher.Controls
import Speecher.Backend

// What removing Speecher did, from Removal.report, with Quit Speecher. General
// shows it under its row; Main shows only it once the settings are deleted.
Notice {
    id: report

    readonly property bool _removed: Removal.report.steps !== undefined
    readonly property bool _failed: _removed && failed(Removal.report)

    readonly property var t: ({
        removed: "Speecher is removed from this computer.",
        removedPartly: "Speecher is removed, with some things left to do by hand.",
        quit: "Quit Speecher"
    })
    // How each of Removal.report's steps reads, by part and outcome. A
    // failure gets the part's own error, which may be empty.
    readonly property var stepWords: ({
        shortcutRegistration: { removed: "Removed the global shortcut registration.",
            failed: e => e ? "Global shortcut: " + e : "The global shortcut registration could not be removed." },
        virtualKeyboardService: { failed: e => "Virtual keyboard service: " + e },
        virtualKeyboard: { removed: "Removed the virtual keyboard setup.",
            failed: e => e ? "Virtual keyboard: " + e : "The virtual keyboard setup could not be removed." },
        keyHelper: { removed: "Removed the single-key helper.",
            failed: e => e ? "Key helper: " + e : "The single-key helper could not be removed." },
        keyring: { removed: "Deleted your API keys from the desktop keyring.",
            failed: e => "Could not delete your API keys from the desktop keyring" + (e ? ": " + e : ".") },
        settings: { removed: "Deleted your settings.", kept: "Kept your settings.",
            keptHidingKeys: "Deleted your settings, except a note that stops Speecher from using the API keys the keyring couldn't delete.",
            failed: e => "Could not delete the settings file at " + e + "." }
    })
    function failed(removal) { return removal.files.failed.length > 0 || removal.steps.some(s => s.outcome === "failed") }
    // What removing did, item by item, then what it couldn't, then what is
    // left: the program file stays.
    function said(removal) {
        const step = s => s.outcome === "failed" ? stepWords[s.part].failed(s.error) : stepWords[s.part][s.outcome]
        return removal.files.removed.map(item => "Removed the " + item + ".")
            .concat(removal.files.absent.map(item => "There was no " + item + " to remove."),
                    removal.steps.filter(s => s.outcome !== "failed").map(step),
                    removal.files.failed.map(item => "Could not remove the " + item + "."),
                    removal.steps.filter(s => s.outcome === "failed").map(step),
                    [removal.programFile ? "To finish, quit Speecher and delete " + removal.programFile + "."
                                         : "To finish, quit Speecher and delete its program file."])
            .join(" ")
    }

    tone: _failed ? "warning" : "info"
    indicator: "icon"
    text: !_removed ? "" : (_failed ? t.removedPartly : t.removed) + " " + said(Removal.report)
    actionText: t.quit
    onAction: System.quit()
}
