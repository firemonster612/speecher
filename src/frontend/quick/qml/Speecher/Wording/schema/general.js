.pragma library

var rows = [
    { id: "themeControl", page: "appearance", section: "", kind: "custom",
      label: "Theme", help: "Follow your desktop, or pick one.",
      keywords: "appearance dark light mode colour color scheme",
      options: [
          { id: "system", label: "System" },
          { id: "light", label: "Light" },
          { id: "dark", label: "Dark" }
      ] },
    // A scheme's id: one of Theme.presets, "system", or "installed:<id>" for
    // one of Appearance.installedSchemes.
    { id: "colorScheme", page: "appearance", section: "", kind: "custom",
      label: "Colors", help: "Each comes in light and dark, and follows the theme.",
      keywords: "colour color scheme palette accent tint kde plasma desktop installed" },
    { id: "windowLayout", page: "appearance", section: "", kind: "custom",
      label: "Layout", help: "Pages on a sheet framed by the window, or beside the sidebar edge to edge.",
      keywords: "split framed sidebar frame border navigation window",
      options: [
          { id: "framed", label: "Framed" },
          { id: "split", label: "Split" }
      ] },
    { id: "translucency", page: "appearance", section: "", kind: "toggle",
      label: "Translucent sidebar", help: "Your desktop shows through the sidebar, blurred.",
      disabledHelp: "Your desktop can't blur behind windows, or has transparency turned down.",
      needsBlur: true, keywords: "transparency transparent blur glass vibrancy mica acrylic background" },
    { id: "roundedCorners", page: "appearance", section: "", kind: "toggle",
      label: "Rounded corners", help: "Round the window's corners, or keep them square.",
      keywords: "corner radius round square window frame border" },
    { id: "launchAtLogin", page: "general", section: "Startup", kind: "toggle",
      label: "Start Speecher at login", help: "Dictation only works while Speecher is running.",
      keywords: "startup autostart boot launch", needs: "canLaunchAtLogin" },
    // Where Speecher can't add itself to startup: how to do it by hand.
    { id: "launchAtLoginGuide", page: "general", section: "Startup", kind: "custom",
      label: "Start Speecher at login",
      help: "Speecher can't add itself to startup on Linux yet. Add it in your desktop's autostart settings, such as System Settings › Autostart on Plasma or Startup Applications in GNOME Tweaks.",
      keywords: "startup autostart boot launch", needs: "!canLaunchAtLogin" },

    // The checks: each row's state, status and fix come from System.checks,
    // by the same id, and each sits on the page of what it gates. Home lists
    // them all.
    { id: "microphoneAccess", page: "microphone", section: "Input", kind: "custom",
      label: "Microphone access", keywords: "permission privacy recording input" },
    { id: "desktopAccessibility", page: "pasting", section: "", kind: "custom",
      label: "Desktop accessibility",
      keywords: "at-spi permission paste rules context learning target app chromium electron chrome" },
    { id: "globalShortcuts", page: "dictation", section: "How shortcuts reach Speecher", kind: "custom",
      label: "Global shortcuts", keywords: "hotkey portal kde registered permission" },
    { id: "virtualKeyboardHealth", page: "pasting", section: "", kind: "custom",
      label: "Virtual keyboard", keywords: "typing uinput ydotool delivery paste password",
      needs: "hasVirtualKeyboard" },

    { id: "runSetup", page: "general", section: "Setup assistant", kind: "action",
      label: "Setup assistant", help: "Go through the first-run steps again.",
      actionLabel: "Run setup", keywords: "wizard onboarding first run permissions checks" },

    { id: "copyToPhone", page: "general", section: "Your phone", kind: "custom",
      label: "Copy settings to your phone",
      help: "Scan the code with Speecher for Android. Your phone needs to be on the same network.",
      keywords: "android mobile qr code transfer export vocabulary snippets profiles" },

    { id: "insightsEnabled", page: "general", section: "Insights", kind: "toggle",
      label: "Keep insights about your dictation",
      help: "Word counts, times and app names, never text or audio. Stays on this computer.",
      offHelp: "Nothing new is recorded while this is off. History you already have stays until you clear it.",
      keywords: "stats statistics history privacy tracking" },
    { id: "clearInsights", page: "general", section: "Insights", kind: "custom",
      label: "Insights history", help: "Delete every recorded dictation from this computer.",
      keywords: "delete clear erase privacy reset stats" },

    { id: "removeSpeecher", page: "general", section: "Uninstall", kind: "custom",
      label: "Remove Speecher",
      help: "Removes the app menu entry, the speecher command, the icon and the global shortcut.",
      keywords: "uninstall delete appimage", needs: "canRemove" },
    // Where Speecher can't remove itself: how the system does it.
    { id: "removeSpeecherGuide", page: "general", section: "Uninstall", kind: "custom",
      label: "Remove Speecher",
      help: Qt.platform.os === "osx" ? "Quit Speecher from its menu bar icon, then move Speecher from Applications to the Trash."
          : Qt.platform.os === "windows" ? "Uninstall it in Windows Settings › Apps › Installed apps."
          : "Remove it with the package manager you installed it with.",
      keywords: "uninstall delete trash", needs: "!canRemove" }
];
