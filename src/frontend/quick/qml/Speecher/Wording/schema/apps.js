.pragma library
.import "pasting.js" as Pasting

// The Apps page's rows and wording: the person's Apps, the App Types, and
// the picker that adds an app. Type ids are core's category names, Pasting
// choice ids its paste method names, and an empty id is "Same as …".

var mac = Qt.platform.os === "osx";
var windows = Qt.platform.os === "windows";

// The App Types, Other apps last; Apps.types holds their settings under the
// same ids. one names a single app of the type. The examples are some of each platform's apps.
var types = [
    { id: "terminal", label: "Terminals", one: "Terminal", icon: "square-terminal",
      examples: mac ? "Terminal, iTerm2, Ghostty" : windows ? "Windows Terminal, PowerShell" : "Konsole, Ghostty, Kitty" },
    { id: "browser", label: "Browsers", one: "Browser", icon: "globe",
      examples: mac ? "Safari, Chrome, Firefox" : windows ? "Edge, Chrome, Firefox" : "Firefox, Chrome, Helium" },
    { id: "email", label: "Email apps", one: "Email app", icon: "mail",
      examples: mac ? "Mail, Outlook" : windows ? "Outlook, Thunderbird" : "Thunderbird, KMail" },
    { id: "office", label: "Office apps", one: "Office app", icon: "file-text",
      examples: mac ? "Pages, Word" : windows ? "Word, Excel" : "LibreOffice Writer" },
    { id: "code_editor", label: "Code editors", one: "Code editor", icon: "code",
      examples: mac ? "Xcode, VS Code, JetBrains" : windows ? "VS Code, Visual Studio, JetBrains" : "Kate, VS Code, JetBrains" },
    { id: "ai_coding", label: "AI coding apps", one: "AI coding app", icon: "bot", examples: "Cursor, Claude Code" },
    { id: "general", label: "Other apps", one: "Other app", icon: "app-window", examples: "Every app Speecher doesn't recognize" }
];
function type(id) { return types.find(t => t.id === id) || types[types.length - 1] }

// What a Pasting choice does to the steps, by paste method. Terminals press
// ⌘V on macOS too, so it has no Terminal keys.
var pastingChoices = [
    { id: "standard_paste", label: "Use the steps on Pasting", icon: "list-checks" },
    { id: "terminal_paste", label: "Terminal keys", help: "The steps, pressing " + Pasting.terminalKeys + ".", icon: "square-terminal" },
    { id: "direct_insert", label: "Skip paste keys", help: "Inserts through accessibility and never presses the paste keys.", icon: "text-cursor-input" },
    { id: "clipboard_only", label: "Clipboard only", help: "Leaves the words on the clipboard for you to paste.", icon: "clipboard" }
].filter(o => o.id !== "terminal_paste" || !mac);
function pastingLabel(id) { return (pastingChoices.find(o => o.id === (id || "standard_paste")) || pastingChoices[0]).label }

// The editor's words, an App's and an App Type's alike.
var t = {
    addApp: "Add app",
    yourApps: "Your apps",
    yourAppsHelp: "Apps you've set something for. Every other app follows its type.",
    noApps: "No apps of your own yet",
    noAppsHelp: "Add one to give it its own writing profile or way of pasting.",
    types: "App types",
    typesHelp: count => "Speecher knows " + count + " common apps by name and sorts them into these types. Pick one with Add app to change it.",
    type: "Type",
    profile: "Writing profile",
    pasting: "Pasting",
    recognizedAs: "Recognized as",
    byName: "Name contains",
    byTitle: "Window title contains",
    titleOnlyType: "A window title says what a window shows, not which app it is, so it doesn't set the type.",
    terminalsNote: "Anything running in a terminal pastes like Terminals unless its app has its own Pasting.",
    remove: "Remove app",
    removeHelp: "It goes back to Automatic everywhere. What Speecher learned in it stays.",
    automatic: label => "Automatic (" + label + ")",
    theSteps: "the steps on Pasting",
    otherProfileHelp: "The profile for apps Speecher doesn't recognize.",
    // The picker's words.
    search: "Search apps",
    recent: "Recently dictated into",
    installed: "Installed",
    scanning: "Looking for installed apps…",
    noMatch: "No app by that name",
    hasSettings: "Has settings",
    moreInstalled: count => count + " more. Type to narrow the list.",
    notListed: "Not in the list?",
    switchToIt: "Switch to it",
    switchHelp: "Pick the app you're in after you switch to it.",
    switching: seconds => seconds > 0 ? "Switch to the app now. Capturing in " + seconds + "…" : "Waiting for you to switch…",
    switchFailed: "Speecher couldn't tell which app that was. Try matching it by name or window title.",
    matchByText: "Match by name or window title…",
    matchHelp: "For web apps and programs that run in a terminal.",
    matchText: "Text",
    matchPlaceholder: "Gmail",
    add: "Add",
    lastSeen: "dictated into"
};

// Why Apps refused an edit, by core's reason.
var refusals = {
    blank: "Type what to match the app by.",
    taken: "Another app is matched by that already.",
    missing: "It, or the profile it picks, was deleted meanwhile.",
    invalid: "Speecher couldn't save this."
};

// What an app's row says it sets: "Email profile, Clipboard only", or
// "Same as its type" when nothing.
function summary(entry, profileName) {
    const parts = []
    if (entry.type) parts.push(type(entry.type).one)
    if (entry.profile) parts.push(profileName(entry.profile) + " profile")
    if (entry.pasting) parts.push(pastingLabel(entry.pasting))
    return parts.length ? parts.join(", ") : "Same as " + type(entry.effective.type).label
}
// What an App Type's row says: its profile and Pasting choice.
function typeSummary(settings, profileName) {
    const profile = settings.profile ? profileName(settings.profile) + " profile" : "Same as Other apps"
    return settings.pasting ? profile + ", " + pastingLabel(settings.pasting) : profile
}

var rows = [
    { id: "apps", page: "apps", section: t.yourApps, kind: "custom",
      label: t.yourApps, help: t.yourAppsHelp,
      keywords: "app per-app application exception rule recognition recognize match desktop id bundle program window title profile paste" },
    { id: "addApp", page: "apps", section: t.yourApps, kind: "custom",
      label: t.addApp, help: "Pick an installed app, or one you dictated into.",
      keywords: "new app exception single app installed switch match name window title web app" },
    { id: "appTypes", page: "apps", section: t.types, kind: "custom",
      label: t.types, help: "Each type has a writing profile and a way of pasting its apps follow.",
      keywords: "app type category terminal keys built-in " + types.map(c => c.label.toLowerCase()).join(" ") },
    { id: "otherAppsProfile", page: "apps", section: t.types, kind: "custom",
      label: "Profile for apps Speecher doesn't recognize", help: t.otherProfileHelp,
      keywords: "fallback default profile unknown unrecognized other apps writing profile" }
];
