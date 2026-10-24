.pragma library

var rows = [
    { id: "themeControl", page: "general", section: "Appearance", kind: "custom",
      label: "Theme", help: "Follow your desktop, or pick one.", value: "system",
      keywords: "appearance dark light mode colour color scheme",
      options: [
          { id: "system", label: "System" },
          { id: "light", label: "Light" },
          { id: "dark", label: "Dark" }
      ] },
    { id: "launchAtLogin", page: "general", section: "Appearance", kind: "toggle",
      label: "Start Speecher at login", help: "Dictation only works while Speecher is running.",
      value: true, keywords: "startup autostart boot launch" },

    // Each health row's state, status and fix come from System.checks.
    { id: "microphoneAccess", page: "general", section: "Permissions & health", kind: "custom",
      label: "Microphone access", keywords: "permission privacy recording input" },
    { id: "desktopAccessibility", page: "general", section: "Permissions & health", kind: "custom",
      label: "Desktop accessibility",
      keywords: "at-spi permission paste rules context learning target app" },
    { id: "globalShortcuts", page: "general", section: "Permissions & health", kind: "custom",
      label: "Global shortcuts", keywords: "hotkey portal kde registered permission" },
    { id: "virtualKeyboardHealth", page: "general", section: "Permissions & health", kind: "custom",
      label: "Virtual keyboard", keywords: "typing uinput ydotool delivery paste password" },
    { id: "runSetup", page: "general", section: "Permissions & health", kind: "action",
      label: "Setup assistant", help: "Go through the first-run steps again.",
      actionLabel: "Run setup", keywords: "wizard onboarding first run" },

    { id: "copyToPhone", page: "general", section: "Your phone", kind: "custom",
      label: "Copy settings to your phone",
      help: "Scan the code with Speecher for Android. Your phone needs to be on the same network.",
      keywords: "android mobile qr code transfer export vocabulary snippets profiles" },

    { id: "insightsEnabled", page: "general", section: "Insights", kind: "toggle",
      label: "Keep insights about your dictation",
      help: "Word counts, times and app names, never text or audio. Stays on this computer.",
      offHelp: "Nothing new is recorded while this is off. History you already have stays until you clear it.",
      value: true, keywords: "stats statistics history privacy tracking" },
    { id: "clearInsights", page: "general", section: "Insights", kind: "custom",
      label: "Insights history", help: "Delete every recorded dictation from this computer.",
      keywords: "delete clear erase privacy reset stats" },

    { id: "removeSpeecher", page: "general", section: "Uninstall", kind: "custom",
      label: "Remove Speecher",
      help: "Removes the app menu entry, the speecher command, the icon and the Global Shortcut.",
      keywords: "uninstall delete appimage" }
];
