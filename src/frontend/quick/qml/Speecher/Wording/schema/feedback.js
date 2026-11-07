.pragma library

var rows = [
    { id: "showLiveText", page: "feedback", section: "Popup", kind: "toggle",
      label: "Show live text while you speak", help: "The popup shows your latest words as they are heard.",
      // Said when the speech service in use only sends text at the end.
      afterStopHelp: "%1 sends its text after you stop, so the popup shows it then.",
      keywords: "preview transcript overlay" },
    { id: "showRefinementText", page: "feedback", section: "Popup", kind: "toggle",
      label: "Show live text during refinement", help: "Watch the cleaned-up text arrive before it is pasted.",
      noRefinerHelp: "Refinement's service is None, so there is no cleaned-up text to show.",
      keywords: "streaming preview cleanup" },
    { id: "previewWords", page: "feedback", section: "Popup", kind: "slider",
      label: "Live text length", help: "Words the popup keeps in view: up to 8 fit on one line, up to 20 on two, more on three.",
      min: 4, max: 30, step: 1, unit: "words", keywords: "words count", enabledWhen: { id: "showLiveText", equals: true },
      disabledHelp: "Turn on live text above to set its length." },
    // Core stores 0 to 5 seconds; 0 doesn't show it.
    { id: "resultDuration", page: "feedback", section: "Popup", kind: "slider",
      label: "Show the result for", help: "How long the popup stays after pasting. Set it to 0 to not show it.",
      min: 0, max: 5000, step: 50, unit: "ms", keywords: "receipt dismiss timeout" },
    // Core's boringMode: true shows the level bars.
    { id: "boringMode", page: "feedback", section: "Popup", kind: "custom",
      label: "Voice display", help: "How the popup shows your voice while you dictate.",
      options: [ { id: "ribbon", label: "Ribbon" }, { id: "bars", label: "Level bars" } ],
      keywords: "boring mode popup waveform level bars meter ribbon animation particles plain simple calm" },
    { id: "playSounds", page: "feedback", section: "Sound", kind: "toggle",
      label: "Play start and stop sounds", help: "A soft chime when dictation starts and stops.",
      keywords: "audio chime beep" },
    { id: "pauseMedia", page: "feedback", section: "Sound", kind: "toggle",
      label: "Pause media while dictating", help: "Pauses music and videos, then resumes them when you stop.",
      keywords: "music spotify youtube mpris" }
];
