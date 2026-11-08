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
      usableWhen: "blur", keywords: "transparency transparent blur glass vibrancy mica acrylic background" },
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
    // them all. A check whose row there is a delivery step names it in
    // opens: Home opens that row, and search finds the step instead.
    { id: "microphoneAccess", page: "microphone", section: "Input", kind: "custom",
      label: "Microphone access", keywords: "permission privacy recording input" },
    { id: "desktopAccessibility", page: "pasting", section: "", kind: "custom",
      label: Qt.platform.os === "osx" ? "Accessibility" : "Desktop accessibility", opens: "insertThroughAccessibility",
      keywords: "at-spi permission paste rules context learning target app chromium electron chrome" },
    { id: "globalShortcuts", page: "dictation", section: "How shortcuts reach Speecher", kind: "custom",
      label: "Global shortcuts", keywords: "hotkey portal kde registered permission" },
    { id: "virtualKeyboardHealth", page: "pasting", section: "", kind: "custom",
      label: "Virtual keyboard", opens: "pressPasteKeys", keywords: "typing uinput ydotool delivery paste password",
      needs: "hasVirtualKeyboard" },

    { id: "runSetup", page: "general", section: "Setup assistant", kind: "action",
      label: "Setup assistant", help: "Go through the first-run steps again.",
      actionLabel: "Run setup", keywords: "wizard onboarding first run permissions checks" },

    { id: "copyToPhone", page: "general", section: "Your phone", kind: "custom",
      label: "Copy settings to your phone",
      help: "Scan the code with Speecher for Android. Your phone needs to be on the same network.",
      keywords: "android mobile qr code transfer export vocabulary snippets profiles" },

    // Privacy & data, General's subpage: every choice about what Speecher
    // keeps or shares, each in one place. History's own rows are edited
    // there; the rest are links to the row that owns the setting, saying how
    // it is set now.
    // What Speecher Keeps. Lowering it below Text asks whether to delete the
    // text already kept (GeneralPage).
    { id: "historyKeeps", page: "privacy", section: "History", kind: "custom",
      label: "What Speecher keeps",
      help: "Stays on this computer. Password fields are never kept.",
      keywords: "history insights stats statistics privacy tracking text audio record save keep",
      options: [
          { id: "nothing", label: "Nothing", help: "Home and History stay empty." },
          { id: "stats", label: "Stats", help: "Word counts, times and app names for Home, never your words." },
          { id: "text", label: "Text", help: "Also what you said, so you can find it in History." },
          { id: "audio", label: "Text and audio", help: "Also the sound of each dictation, to play it back." }
      ] },
    { id: "historyTextDays", page: "privacy", section: "History", kind: "choice", control: "select",
      label: "Keep text for",
      help: "Older dictations and file transcriptions keep their stats. Their words and audio are deleted.",
      keywords: "history retention days delete older expire",
      hiddenWhen: { id: "historyKeeps", equals: ["nothing", "stats"] },
      options: [
          { id: "7", label: "7 days" },
          { id: "30", label: "30 days" },
          { id: "90", label: "90 days" },
          { id: "365", label: "A year" },
          { id: "0", label: "Until I delete it" }
      ] },
    { id: "historyAudioDays", page: "privacy", section: "History", kind: "choice", control: "select",
      label: "Keep audio for",
      help: "About 0.25 MB a minute. Never longer than text.",
      keywords: "history retention days audio sound recording delete",
      hiddenWhen: { id: "historyKeeps", equals: ["nothing", "stats", "text"] },
      options: [
          { id: "1", label: "A day" },
          { id: "7", label: "7 days" },
          { id: "30", label: "30 days" },
          { id: "-1", label: "As long as text" }
      ] },
    // The Apps and App Types whose History choice keeps less, read-only:
    // each is changed in its editor on Apps.
    { id: "historyExcludedApps", page: "privacy", section: "History", kind: "custom",
      label: "Apps left out of History",
      help: "Apps and app types that keep less than the choice above. Change them in Apps.",
      emptyText: "Every app follows the choice above.",
      keywords: "history exclude excluded private password manager incognito app per-app stats nothing privacy" },
    { id: "clearHistory", page: "privacy", section: "History", kind: "custom",
      label: "Clear history",
      help: "Deletes text and audio from this computer. Check Also delete their stats to empty Home too.",
      keywords: "delete clear erase privacy reset stats insights history" },

    { id: "privacyRefinementSees", page: "privacy", section: "Outside History", kind: "custom",
      label: "What refinement sees",
      help: "Besides your words, what Speecher sends the refinement service for each dictation. Never kept afterwards.",
      keywords: "privacy context nearby text cursor screenshot screen share send cloud refinement" },
    { id: "privacyLearnedCorrections", page: "privacy", section: "Outside History", kind: "custom",
      label: "Learning from your edits",
      help: "Corrections keep the words you fixed and the app you fixed them in.",
      keywords: "privacy learned corrections edits watch learn vocabulary" },
    // Held by the daemon, not saved, so there is nothing to set.
    { id: "privacyLastText", page: "privacy", section: "Outside History", kind: "custom",
      label: "Your last dictation",
      help: "Its text stays in memory for Paste again, Home and speecher last until Speecher quits, whatever History keeps.",
      keywords: "privacy last latest transcript memory paste again clipboard" },

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
