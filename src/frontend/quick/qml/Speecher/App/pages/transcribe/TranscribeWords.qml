pragma Singleton
import QtQuick
import Speecher.Wording

// The Transcribe page's wording, and the helpers that word Transcription's
// files, phases and results.
QtObject {
    readonly property string audioFiles: "Audio files"
    readonly property string transcription: "Transcription"
    readonly property string refinement: "Refinement"
    readonly property string output: "Output"
    readonly property string dropToAdd: "Drop audio or video files here, or choose them"
    readonly property string dropMore: "Drop more files here"
    readonly property string mediaHint: "MP3, M4A, WAV, FLAC, OGG, Opus, MP4, MOV and WebM"
    readonly property string chooseFiles: "Choose audio files…"
    readonly property string addMore: "Add more files…"
    readonly property string filesDialog: "Choose audio or video files"
    readonly property string mediaFiles: "Audio and video files"
    readonly property string allFiles: "All files"
    readonly property string serviceHelp: "The speech-to-text service that hears the files."
    readonly property string language: "Spoken language"
    readonly property string languageHelp: "The language the files are spoken in."
    readonly property string vocabulary: "Apply vocabulary"
    readonly property string vocabularyHelp: "Spell your names and terms the way Vocabulary has them."
    readonly property string refinerHelp: "Cleans up the raw transcripts with a language model."
    readonly property string model: "Model"
    readonly property string modelHint: "Change it in Refinement settings."
    readonly property string cleanup: "Cleanup level"
    readonly property string cleanupHelp: "How much the model may rewrite."
    readonly property string profile: "Writing Profile"
    readonly property string profileHelp: "Sets the services, cleanup and tone for these files."
    readonly property string tone: "Tone"
    readonly property string toneHelp: "Overrides the profile's tone, if set."
    readonly property string needsRefiner: "Choose a provider above to use this."
    readonly property string saveTranscripts: "Save transcripts"
    readonly property string folder: "Folder"
    readonly property string changeFolder: "Change…"
    readonly property string folderDialog: "Choose the folder for transcripts"
    readonly property string noFilesYet: "Add at least one audio file to start."
    readonly property string partialName: "Transcript so far"
    readonly property string partialPlaceholder: "The transcript appears here as it is heard."
    readonly property string partialHelp: "Each part appears as soon as it is heard."
    readonly property string refined: "Refined"
    readonly property string raw: "Raw"
    readonly property string copyAll: "Copy all"
    readonly property string exportAll: "Export all"
    readonly property string export_: "Export"
    readonly property string exportDialog: "Export the transcript"
    readonly property string exportAllDialog: "Choose the folder to export into"
    readonly property string saved: "Saved"
    readonly property string retry: "Retry"
    readonly property string retrying: "Retrying…"
    readonly property string transcribeMore: "Transcribe more files"
    readonly property string showAll: "Show all"
    readonly property string showLess: "Show less"

    readonly property var steps: [
        { id: "configure", label: "Configure" },
        { id: "transcribe", label: "Transcribe" },
        { id: "export", label: "Export" }
    ]
    readonly property var phases: ({
        reading: "Reading the audio…",
        transcribing: "Transcribing…",
        finishing: "Finishing the transcript…",
        refining: "Refining…"
    })
    readonly property var queueLabels: ({ done: "Done", failed: "Failed", waiting: "Waiting" })
    readonly property var destinations: [
        { id: "none", label: "Just show them here", icon: "eye" },
        { id: "beside", label: "Next to each audio file", icon: "file-text" },
        { id: "folder", label: "One folder…", icon: "folder" }
    ]
    function destinationHint(destination) {
        return destination === "beside" ? "Each transcript is saved as ⟨name⟩-transcribed.txt"
             : destination === "none" ? "Copy or export from the results afterwards" : ""
    }
    // Each format with the file type its save dialog names.
    readonly property var exportFormats: [
        { id: "text", label: "Text file…", icon: "file-text", fileType: "Text files" },
        { id: "srt", label: "SRT subtitles…", icon: "captions", fileType: "SubRip subtitles" },
        { id: "vtt", label: "WebVTT subtitles…", icon: "captions", fileType: "WebVTT subtitles" }
    ]
    // The file chooser's filters: the files it may take, then anything, since
    // a file's content decides, not its name.
    function fileFilters(extensions) {
        return [mediaFiles + " (" + extensions.map(e => "*." + e).join(" ") + ")", allFiles + " (*)"]
    }
    // The save dialog's filter for a format, from the file it suggests.
    function exportFilter(format, file) {
        return exportFormats.find(f => f.id === format).fileType + " (*." + file.toString().split(".").pop() + ")"
    }

    // "3 min 7 s", or "42 s" under a minute.
    function durationLabel(ms) {
        const s = Math.round(ms / 1000)
        return s < 60 ? s + " s" : Math.floor(s / 60) + " min " + (s % 60) + " s"
    }
    function sizeLabel(bytes) { return bytes >= 1e9 ? (bytes / 1e9).toFixed(1) + " GB" : (bytes / 1e6).toFixed(1) + " MB" }
    // A file's length and size; the size alone while its length is unknown.
    function fileDetail(f) { return (f.durationMs >= 0 ? durationLabel(f.durationMs) + ", " : "") + sizeLabel(f.bytes) }
    // "1 h 7 min of audio", or "3 min 7 s of audio" under an hour, from the
    // lengths that are known.
    function audioLength(list) {
        const ms = list.reduce((t, f) => t + Math.max(0, f.durationMs), 0)
        const min = Math.round(ms / 60000)
        return (min >= 60 ? Math.floor(min / 60) + " h " + (min % 60) + " min" : durationLabel(ms)) + " of audio"
    }
    function startCaption(count) { return count > 1 ? "Transcribe " + count + " files" : "Transcribe" }
    // The files list's caption: what will be transcribed, and by whom.
    function filesCaption(files, pipeline) {
        return files.length ? Words.count(files.length, "file", "files") + ", " + audioLength(files) + ". " + pipeline + "." : noFilesYet
    }
    function processingCount(current, count) { return "File " + (current + 1) + " of " + count }
    function wordsLabel(n) { return Words.count(n, "word", "words") }
    function savedLabel(path) { return "Saved to " + path }
    function resultsTitle(results) { return results.filter(r => !r.failed).length === 1 ? "Transcript" : "Transcripts" }
    // The line above the results: how many finished, from how much audio,
    // who heard and refined them, and where they were saved.
    function batchSummary(batch, results) {
        const failed = results.filter(r => r.failed).length
        const done = results.length - failed
        const count = batch.cancelled ? "Cancelled after " + done + " of " + batch.files.length + " files"
                                      : done + " done"
        const saved = results.some(r => r.savedPath)
        const saving = !saved ? "" : batch.destination === "folder" ? ", and saved in " + batch.folder
                                                                    : ", and saved next to each audio file"
        return count + (failed ? " and " + failed + " failed" : "") + ", from " + audioLength(results) + ". "
            + "Heard by " + batch.speech + (batch.refinement ? ", cleaned up by " + batch.refinement : "") + saving + "."
    }
    // Where subtitles come from, or why there are none; empty when no file
    // finished.
    function subtitlesNote(batch, results) {
        const finished = results.filter(r => !r.failed)
        if (!finished.length) return ""
        if (!finished.some(r => r.timings)) return "Subtitles need timings, and " + batch.speech + " returned none."
        return batch.refines ? "Subtitles come from the Raw Transcript, since refinement rewrites the words its timings belong to."
                             : "Subtitles come from the Raw Transcript."
    }
}
