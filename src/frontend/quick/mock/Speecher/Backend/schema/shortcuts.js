.pragma library

var rows = [
    { id: "globalShortcut", page: "shortcuts", section: "Dictation", kind: "shortcut",
      label: "Dictation shortcut", help: "Starts and stops dictation from any app.",
      value: ["Ctrl", "Space"], keywords: "hotkey global keybinding start stop" },
    { id: "shortcutBehavior", page: "shortcuts", section: "Dictation", kind: "choice", control: "cards",
      label: "When you press it", value: "hybrid", keywords: "push to talk toggle hold hybrid mode",
      options: [
          { id: "pushToTalk", label: "Push to talk", icon: "hand", help: "Dictate only while the key is held." },
          { id: "toggle", label: "Toggle", icon: "toggle-right", help: "One press starts, the next press stops." },
          { id: "hybrid", label: "Hybrid", icon: "timer", help: "Tap to toggle, or hold to dictate until release." }
      ] },
    { id: "cancelShortcut", page: "shortcuts", section: "While dictating", kind: "shortcut",
      label: "Cancel", help: "Throws away the dictation in progress.",
      value: ["Esc"], keywords: "abort discard stop" },
    { id: "pauseShortcut", page: "shortcuts", section: "While dictating", kind: "shortcut",
      label: "Pause and resume", help: "Pauses the dictation in progress. Press again to resume.",
      value: ["Ctrl", "Shift", "Space"], keywords: "hold break" },
    { id: "startCommand", page: "shortcuts", section: "How shortcuts reach Speecher", kind: "custom",
      label: "Start from a terminal or your own keybinding",
      help: "Runs one dictation into the focused app. Add --profile or --tone to override them for that dictation.",
      icon: "square-terminal", command: "speecher --start-listening", keywords: "command line cli terminal script keybinding start listening" }
];
