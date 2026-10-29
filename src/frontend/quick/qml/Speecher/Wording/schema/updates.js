.pragma library

var rows = [
    { id: "updateChannel", page: "updates", section: "How Speecher updates", kind: "choice", control: "segmented",
      label: "Update channel", keywords: "nightly stable beta release builds",
      options: [
          { id: "stable", label: "Stable", icon: "shield-check", help: "Hand-tested releases." },
          { id: "nightly", label: "Nightly", icon: "flask-conical", help: "Untested builds from every push to master." }
      ] },
    { id: "autoCheckUpdates", page: "updates", section: "How Speecher updates", kind: "toggle",
      label: "Check for updates automatically",
      keywords: "auto background check" },
    { id: "updateCheckInterval", page: "updates", section: "How Speecher updates", kind: "choice", control: "select",
      label: "Check frequency",
      enabledWhen: { id: "autoCheckUpdates", equals: true }, keywords: "interval how often schedule",
      help: "How often Speecher looks for a new version.",
      disabledHelp: "Turn on automatic checks to choose how often.",
      options: [
          { id: "5", label: "Every 5 minutes" },
          { id: "15", label: "Every 15 minutes" },
          { id: "30", label: "Every 30 minutes" },
          { id: "60", label: "Every hour" },
          { id: "360", label: "Every 6 hours" },
          { id: "720", label: "Every 12 hours" },
          { id: "1440", label: "Every day" },
          { id: "10080", label: "Every week" },
          { id: "custom", label: "Custom" }
      ] },
    { id: "updateCheckCustomInterval", page: "updates", section: "How Speecher updates", kind: "custom",
      label: "Custom interval", help: "Between 5 minutes and 30 days.",
      keywords: "interval minutes hours days",
      options: [
          { id: "minutes", label: "Minutes" },
          { id: "hours", label: "Hours" },
          { id: "days", label: "Days" }
      ] },
    { id: "autoInstallUpdates", page: "updates", section: "How Speecher updates", kind: "toggle",
      label: "Download and install updates automatically",
      help: "Updates install in the background and take effect after a restart.",
      keywords: "auto install background appimage" }
];

// Each settings row's Since Version, by row id, for What's New's new
// settings. A row reached only through another, such as the custom check
// interval, is left out.
var since = {
    updateChannel: "0.1.0",
    autoCheckUpdates: "0.1.0",
    autoInstallUpdates: "0.1.0",
    shortcutBehavior: "0.1.6",
    codexFinalRetranscribe: "0.1.6",
    cliproxyOauthDir: "0.2.0",
    cancelShortcut: "0.2.1",
    pauseShortcut: "0.2.1",
    updateCheckInterval: "0.2.1",
    insightsEnabled: "0.2.1",
    reviewSelectionEdits: "0.2.1",
    spokenLanguage: "0.2.1",
    roundedCorners: "0.2.1"
};
