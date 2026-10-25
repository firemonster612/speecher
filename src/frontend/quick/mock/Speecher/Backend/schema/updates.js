.pragma library

var rows = [
    { id: "updateChannel", page: "updates", section: "How Speecher updates", kind: "choice", control: "segmented",
      label: "Update channel", value: "stable", keywords: "nightly stable beta release builds",
      options: [
          { id: "stable", label: "Stable", icon: "shield-check", help: "Hand-tested releases." },
          { id: "nightly", label: "Nightly", icon: "flask-conical", help: "Untested builds from every push to master." }
      ] },
    { id: "autoCheckUpdates", page: "updates", section: "How Speecher updates", kind: "toggle",
      label: "Check for updates automatically", value: true,
      keywords: "auto background check" },
    { id: "updateCheckInterval", page: "updates", section: "How Speecher updates", kind: "choice", control: "select",
      label: "Check frequency", value: "1440",
      enabledWhen: { id: "autoCheckUpdates", equals: true }, keywords: "interval how often schedule",
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
      label: "Custom interval", help: "Between 5 minutes and 30 days.", value: { number: 3, unit: "days" },
      keywords: "interval minutes hours days",
      options: [
          { id: "minutes", label: "Minutes" },
          { id: "hours", label: "Hours" },
          { id: "days", label: "Days" }
      ] },
    { id: "autoInstallUpdates", page: "updates", section: "How Speecher updates", kind: "toggle",
      label: "Download and install updates automatically", value: true,
      help: "Updates install in the background and take effect after a restart.",
      keywords: "auto install background appimage" }
];
