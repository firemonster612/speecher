// The wording Transcribe files and Recordings share: what a run makes its
// transcripts with, and where they go.
.pragma library

var text = {
    runWith: "Run with",
    profile: "Writing profile",
    yourSettings: "your settings",
    saveTranscripts: "Save transcripts",
    where: "Where",
    folder: "Folder",
    change: "Change…",
    open: "Open",
    format: "File format"
}

// What the row the run's changes fold under says: "Same as Work", or
// "2 changes".
function changesSummary(count, profileName) {
    return count === 0 ? "Same as " + profileName : count === 1 ? "1 change" : count + " changes"
}

// A folder with the home folder as ~.
function shortPath(path, home) {
    return home && path.indexOf(home) === 0 ? "~" + path.slice(home.length) : path
}
