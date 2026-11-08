.pragma library

// The Recordings page's rows: the recorder, which search lands on, and the
// settings a recording runs with. Its wording beyond them is
// pages/recordings/RecordingWords.js's.
var rows = [
    { id: "recordingControl", page: "recordings", section: "", kind: "custom",
      label: "Record a call",
      help: "Records your microphone as me and what your computer plays as them, and writes what's said to a file as it goes. Nothing is pasted.",
      keywords: "record call meeting start stop transcript minutes interview" },
    { id: "recordingSources", page: "recordings", section: "Recording settings", kind: "choice", control: "segmented",
      label: "Record",
      help: "Call audio is everything your computer plays, so mute music first.",
      fixedHelp: "This computer can't capture what it plays, so recordings take your microphone only.",
      keywords: "system audio speakers them microphone only mic sources",
      options: [
          { id: "both", label: "Microphone and call audio" },
          { id: "mic", label: "Microphone only" }
      ] },
    { id: "recordingService", page: "recordings", section: "Recording settings", kind: "custom",
      label: "Speech service",
      help: "Calls need a service that sends text as you speak. Local models can't, so Speecher skips them.",
      keywords: "provider transcription model realtime chatgpt claude service order" },
    { id: "recordingFolder", page: "recordings", section: "Recording settings", kind: "custom",
      label: "Save transcripts in",
      help: "Each recording gets its own file, named by its name and when it started.",
      actionLabel: "Change", openLabel: "Open", dialogTitle: "Save recordings in",
      keywords: "folder directory location documents path files" },
    { id: "recordingFormat", page: "recordings", section: "Recording settings", kind: "choice", control: "segmented",
      label: "File format",
      help: "Markdown and plain text hold the same lines, [00:01:23] me: …. History exports JSON too.",
      keywords: "markdown md txt plain text extension",
      options: [
          { id: "md", label: "Markdown" },
          { id: "txt", label: "Plain text" }
      ] },
    { id: "recordingKeepAudio", page: "recordings", section: "Recording settings", kind: "toggle",
      label: "Keep the call's audio",
      help: "Saves both sides in one file beside the transcript, so you can play a line back. Ask the people on the call first. About 30 MB an hour.",
      keywords: "audio sound save m4a play back" },
    { id: "recordingAutoStop", page: "recordings", section: "Recording settings", kind: "choice", control: "select",
      label: "Stop after silence",
      help: "Ends a recording you forgot, once neither side has spoken for this long.",
      keywords: "auto stop silence quiet forgot timeout minutes",
      options: [
          { id: "5", label: "5 minutes" },
          { id: "15", label: "15 minutes" },
          { id: "30", label: "30 minutes" },
          { id: "60", label: "An hour" },
          { id: "0", label: "Never" }
      ] },
    { id: "recordingShortcut", page: "recordings", section: "Recording settings", kind: "shortcut",
      label: "Recording shortcut",
      help: "Starts and stops a recording from any app.",
      keywords: "hotkey keybinding start stop record", needs: "hasGlobalShortcuts" },
    { id: "recordingPill", page: "recordings", section: "Recording settings", kind: "toggle",
      label: "Show in the popup",
      help: "A small pill with the time and Stop stays at the bottom of the screen while a recording runs. Screen shares show it too.",
      keywords: "pill indicator overlay popup screen share visible" },
    { id: "recentRecordings", page: "recordings", section: "Recent recordings", kind: "custom",
      label: "Recent recordings",
      help: "Your latest recordings, which History keeps.",
      keywords: "past recordings transcripts history open read export" }
];
