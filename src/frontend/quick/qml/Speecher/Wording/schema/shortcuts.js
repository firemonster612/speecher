.pragma library

// Escape cancels a dictation on Windows and macOS (CONTEXT.md, Cancel
// Shortcut), so there the Cancel row is a key besides it.
var escapeCancels = Qt.platform.os === "windows" || Qt.platform.os === "osx";

// The rows that bind through the desktop need it to give Speecher global
// shortcuts (System.hasGlobalShortcuts); where it gives none, the page and
// search leave them out and the start command is the way in.
var rows = [
    { id: "globalShortcut", page: "dictation", section: "Shortcut", kind: "shortcut",
      label: "Dictation shortcut", help: "Starts and stops dictation from any app.",
      keywords: "hotkey global keybinding start stop", needs: "hasGlobalShortcuts" },
    { id: "shortcutBehavior", page: "dictation", section: "Shortcut", kind: "choice", control: "cards",
      label: "When you press it", keywords: "push to talk toggle hold hybrid mode", needs: "hasGlobalShortcuts",
      options: [
          { id: "push_to_talk", label: "Push to talk", icon: "hand", help: "Dictate only while the key is held." },
          { id: "toggle", label: "Toggle", icon: "toggle-right", help: "One press starts, the next press stops." },
          { id: "hybrid", label: "Hybrid", icon: "timer", help: "Tap to toggle, or hold to dictate until release." }
      ] },
    { id: "cancelShortcut", page: "dictation", section: "While dictating", kind: "shortcut",
      label: escapeCancels ? "Another cancel key" : "Cancel",
      help: escapeCancels ? "Escape already throws away the dictation in progress. Set a key here to do it with another one too."
                          : "Throws away the dictation in progress.",
      keywords: "abort discard stop escape", needs: "hasGlobalShortcuts" },
    { id: "pauseShortcut", page: "dictation", section: "While dictating", kind: "shortcut",
      label: "Pause and resume", help: "Pauses the dictation in progress. Press again to resume.",
      keywords: "hold break", needs: "hasGlobalShortcuts" },
    { id: "pasteAgainShortcut", page: "dictation", section: "After dictating", kind: "shortcut",
      label: "Paste again", help: "Pastes your last dictation again into the app you're in.",
      keywords: "repeat last dictation transcript history paste", needs: "hasGlobalShortcuts" },
    // Set here with the other shortcuts, so their keys are checked in one
    // list; Recordings shows it read-only (setOn, change).
    { id: "recordingShortcut", page: "dictation", section: "Recording a call", kind: "shortcut",
      label: "Record a call",
      help: "Starts a recording with your saved choices, or stops the one running, from any app.",
      setOn: "Set on Dictation, with your other shortcuts.", notSet: "No shortcut yet. Set one on Dictation.",
      change: "Change on Dictation",
      keywords: "recording shortcut hotkey keybinding start stop record call meeting", needs: "hasGlobalShortcuts" },
    // bindHelp says where a desktop that gives no shortcut binds a command
    // to a key, by System.desktop; other is for a desktop Speecher doesn't know.
    { id: "startCommand", page: "dictation", section: "How shortcuts reach Speecher", kind: "custom",
      label: "Start from a terminal or your own keybinding",
      help: "Runs one dictation into the focused app, so run from a terminal it types into that terminal. Add --⁠profile or --⁠tone to override them for that dictation.",
      bindHelp: {
          gnome: "To give it a key, add a custom shortcut in Settings › Keyboard › View and Customize Shortcuts.",
          sway: "To give it a key, add a line such as bindsym $mod+d exec speecher --start-listening to your Sway config.",
          hyprland: "To give it a key, add a line such as bind = SUPER, D, exec, speecher --start-listening to your Hyprland config.",
          niri: "To give it a key, add Mod+D { spawn \"speecher\" \"--start-listening\"; } to the binds in your niri config.",
          other: "To give it a key, bind this command in your desktop's keyboard shortcut settings."
      },
      command: "speecher --start-listening", keywords: "command line cli terminal script keybinding start listening" },
    // Where the desktop gives no shortcut: the commands that do the rest of
    // what the shortcuts would, to bind beside the start command.
    { id: "controlCommands", page: "dictation", section: "How shortcuts reach Speecher", kind: "custom",
      label: "Finish, cancel or paste again",
      help: "Bind these too: speecher stop finishes the dictation and pastes it, speecher cancel throws it away, and speecher toggle starts or finishes it with one key. The popup's buttons pause and cancel. To paste a dictation again, open it in History.",
      commands: ["speecher toggle", "speecher stop", "speecher cancel"], historyLabel: "Open History",
      keywords: "command line cli stop cancel toggle finish paste again keybinding" }
];
