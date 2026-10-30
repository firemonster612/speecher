.pragma library

var rows = [
    { id: "showLiveText", page: "feedback", section: "Popup", kind: "toggle",
      label: "Show live text while you speak", help: "The popup shows your latest words as they are heard.",
      keywords: "preview transcript overlay" },
    { id: "showRefinementText", page: "feedback", section: "Popup", kind: "toggle",
      label: "Show live text during refinement", help: "Watch the cleaned-up text arrive before it is pasted.",
      keywords: "streaming preview cleanup" },
    { id: "previewWords", page: "feedback", section: "Popup", kind: "slider",
      label: "Live text length", help: "Words the popup keeps in view: up to 8 fit on one line, up to 20 on two, more on three.",
      min: 4, max: 30, step: 1, unit: "words", keywords: "words count", enabledWhen: { id: "showLiveText", equals: true },
      disabledHelp: "Turn on live text above to set its length." },
    { id: "resultDuration", page: "feedback", section: "Popup", kind: "choice", control: "select",
      label: "Show the result for", help: "How long the popup stays after pasting.",
      keywords: "receipt dismiss timeout",
      // Milliseconds, which core stores up to 5 seconds.
      options: [
          { id: "0", label: "Don't show it" },
          { id: "500", label: "Half a second" },
          { id: "1000", label: "1 second" },
          { id: "2000", label: "2 seconds" },
          { id: "5000", label: "5 seconds" }
      ] },
    { id: "reviewSelectionEdits", page: "feedback", section: "Popup", kind: "toggle",
      label: "Review edits to selected text", help: "When you dictate over a selection, see what will change and press Replace to apply it.",
      keywords: "diff selection edit confirm" },
    { id: "playSounds", page: "feedback", section: "Sound", kind: "toggle",
      label: "Play start and stop sounds", help: "A soft chime when dictation starts and stops.",
      keywords: "audio chime beep" },
    { id: "pauseMedia", page: "feedback", section: "Sound", kind: "toggle",
      label: "Pause media while dictating", help: "Pauses music and videos, then resumes them when you stop.",
      keywords: "music spotify youtube mpris" }
];
