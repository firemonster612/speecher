.pragma library

var rows = [
    { id: "showLiveText", page: "feedback", section: "Popup", kind: "toggle",
      label: "Show live text while you speak", help: "The popup shows your latest words as they are heard.",
      value: true, keywords: "preview transcript overlay" },
    { id: "showRefinementText", page: "feedback", section: "Popup", kind: "toggle",
      label: "Show live text during refinement", help: "Watch the cleaned-up text arrive before it is pasted.",
      value: true, keywords: "streaming preview cleanup" },
    { id: "previewWords", page: "feedback", section: "Popup", kind: "slider",
      label: "Live text length", help: "How many lines of live text the popup shows, set by word count: up to 8 words is one line, up to 20 is two, more is three.",
      value: 12, min: 4, max: 30, step: 1, unit: "words", keywords: "words count", enabledWhen: { id: "showLiveText", equals: true } },
    { id: "resultDuration", page: "feedback", section: "Popup", kind: "choice", control: "select",
      label: "Show the result for", help: "How long the popup stays after pasting.",
      value: "4", keywords: "receipt dismiss timeout",
      options: [
          { id: "0", label: "Don't show it" },
          { id: "2", label: "2 seconds" },
          { id: "4", label: "4 seconds" },
          { id: "8", label: "8 seconds" },
          { id: "sticky", label: "Until dismissed" }
      ] },
    { id: "reviewSelectionEdits", page: "feedback", section: "Popup", kind: "toggle",
      label: "Review edits to selected text", help: "When you dictate over a selection, see what will change and press Replace to apply it.",
      value: true, keywords: "diff selection edit confirm" },
    { id: "playSounds", page: "feedback", section: "Sound", kind: "toggle",
      label: "Play start and stop sounds", help: "A soft chime when dictation starts and stops.",
      value: true, keywords: "audio chime beep" },
    { id: "pauseMedia", page: "feedback", section: "Sound", kind: "toggle",
      label: "Pause media while dictating", help: "Pauses music and videos, then resumes them when you stop.",
      value: true, keywords: "music spotify youtube mpris" }
];
