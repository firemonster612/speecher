.pragma library

var os = Qt.platform.os;
var mac = os === "osx";
var windows = os === "windows";
var linux = !mac && !windows;

// The paste keys, as each platform presses them, and a terminal's.
var pasteKeys = mac ? "⌘V" : "Ctrl+V";
var terminalKeys = "Ctrl+Shift+V";

// What the delivery steps rely on here, by the need ids System.deliverySteps
// names: label names it in every status ("Virtual keyboard: Not set up"),
// check is the System check whose state and fix it shows, and ready and
// notReady are the state's words where the check's own don't fit or there
// is no check.
var needs = {
    accessibility: mac ? { label: "Accessibility", check: "desktopAccessibility", ready: "Granted", notReady: "Not granted",
                           fix: "Open settings" }
                       : { label: "Desktop accessibility", check: "desktopAccessibility" },
    virtualKeyboard: { label: "Virtual keyboard", check: "virtualKeyboardHealth" },
    clipboard: { label: "Clipboard helper or wl-copy", ready: "Available", notReady: "Missing" },
    clipboardSave: { label: "wl-paste", ready: "Available", notReady: "Missing" }
};
// A step that relies on nothing here says so instead of a need.
var builtIn = windows ? "Built into Windows, no permission needed" : "Built in";
// A step whose need an earlier step shows already, fix and all.
function sameNeed(need) { return "Needs the same " + needs[need].label + " permission"; }
function waitingFor(need) { return "Waiting for " + needs[need].label; }
// A step's switch, as search shows it read-only.
var switchState = { on: "On", off: "Off" };

// The line under each step's help about what it relies on here.
var details = {
    insert: linux ? "On Linux it can't replace selected text yet, so edits to a selection need the paste keys."
          : mac ? "Needs Accessibility under Privacy & Security."
          : "Fields that don't accept it get the paste keys instead.",
    press: linux ? "Speecher's virtual keyboard presses the keys. On Wayland, copying needs Speecher's clipboard helper or wl-copy."
         : mac ? "Needs the same Accessibility permission as inserting."
         : "Windows presses the keys for Speecher.",
    keep: linux ? "On Wayland this needs Speecher's clipboard helper or wl-copy, and with it off, wl-paste to save your clipboard first."
        : ""
};

// Rule options, worded as what they do to the steps above. Ids are core's
// paste methods. The first is the rule's "no change": for an app type no
// rule at all (inherit), for a single app standard_paste, which still wins
// over its app type's rule. Terminals press ⌘V on macOS too, so it has no
// Terminal keys.
function ruleOptions(forApp) {
    return [
        forApp ? { id: "standard_paste", label: "Use the steps above, not its app type's rule",
                   help: "Overrides the rule for the app's type.", icon: "list-checks" }
               : { id: "inherit", label: "Use the steps above", help: "No exception for this app type.", icon: "list-checks" },
        { id: "terminal_paste", label: "Terminal keys", help: "The steps above, pressing " + terminalKeys + ".",
          icon: "square-terminal" },
        { id: "direct_insert", label: "Skip paste keys", help: "Inserts through accessibility and never presses the paste keys.",
          icon: "text-cursor-input" },
        { id: "clipboard_only", label: "Clipboard only", help: "Leaves the words on the clipboard for you to paste.",
          icon: "clipboard" }
    ].filter(o => o.id !== "terminal_paste" || !mac);
}

// The app types Speecher sorts apps into; ids are core's category names, and
// host/QuickSettings saves a rule for each. Library and the Pasting page
// read them from here. The examples are some of each platform's apps.
var categories = [
    { id: "terminal", label: "Terminals", icon: "square-terminal",
      examples: mac ? "Terminal, iTerm2, Ghostty" : windows ? "Windows Terminal, PowerShell" : "Konsole, Ghostty, Kitty" },
    { id: "browser", label: "Browsers", icon: "globe",
      examples: mac ? "Safari, Chrome, Firefox" : windows ? "Edge, Chrome, Firefox" : "Firefox, Chrome, Helium" },
    { id: "email", label: "Email apps", icon: "mail",
      examples: mac ? "Mail, Outlook" : windows ? "Outlook, Thunderbird" : "Thunderbird, KMail" },
    { id: "office", label: "Office apps", icon: "file-text",
      examples: mac ? "Pages, Word" : windows ? "Word, Excel" : "LibreOffice Writer" },
    { id: "code_editor", label: "Code editors", icon: "code",
      examples: mac ? "Xcode, VS Code, JetBrains" : windows ? "VS Code, Visual Studio, JetBrains" : "Kate, VS Code, JetBrains" },
    { id: "ai_coding", label: "AI coding apps", icon: "bot", examples: "Cursor, Claude Code" },
    { id: "general", label: "Other apps", icon: "app-window", examples: "Everything else" }
];

// How a single app is named in a rule, as each platform's target reports it.
var appId = linux ? { label: "Desktop ID", named: "its desktop ID", example: "org.kde.konsole",
                      help: "The app's desktop ID, such as org.kde.konsole. It has to match exactly." }
          : mac ? { label: "Bundle ID", named: "its bundle ID", example: "com.apple.Terminal",
                    help: "The app's bundle ID, such as com.apple.Terminal. It has to match exactly." }
          : { label: "Program name", named: "its program name", example: "windowsterminal",
              help: "The program's file name without .exe, such as windowsterminal. It has to match exactly." };

// The row for each step, by step id.
var rowOf = { insert: "insertThroughAccessibility", press: "pressPasteKeys", keep: "keepOnClipboard" };
var stepLabels = { insert: "Insert through accessibility", press: "Press the paste keys", keep: "Keep the words on the clipboard" };
var stepShort = { insert: "Insert", press: "Press", keep: "Keep" };

// What a rule leaves of the steps, from System.rulePlan(method): "Insert,
// else press Ctrl+V, keep a copy", "Clipboard only", or "No delivery: Press
// is off" where it relies on an off step and nothing is left.
function planText(plan, method) {
    const route = plan.route || [];
    const keys = method === "terminal_paste" ? terminalKeys : pasteKeys;
    const parts = [];
    if (route.includes("insert")) parts.push("Insert");
    if (route.includes("press")) parts.push((parts.length ? "else press " : "Press ") + keys);
    if (route.includes("keep")) parts.push(parts.length ? "keep a copy" : "Clipboard only");
    const off = plan.needs ? stepShort[plan.needs] + " is off" : "";
    if (!parts.length) return off ? "No delivery: " + off : "No delivery with these settings";
    return parts.join(", ") + (off ? ". " + off : "");
}

// The order rules win in, said once above the exceptions.
var precedence = "A single app's exception comes first, then Terminals for anything running in a terminal, AI coding tools included, then the app's type, then the steps above.";

// The step rows are custom: the Pasting page draws each one's switch with
// what it needs, and keeps the last one on.
var rows = [
    { id: "insertThroughAccessibility", page: "pasting", section: "Steps", kind: "custom", step: "insert",
      label: stepLabels.insert, icon: "text-cursor-input",
      help: "Writes your words straight into the field you're in.",
      keywords: "direct insertion type accessibility at-spi ui automation delivery output" },
    { id: "pressPasteKeys", page: "pasting", section: "Steps", kind: "custom", step: "press",
      label: stepLabels.press, icon: "keyboard",
      help: "Copies your words and presses " + pasteKeys + " when insertion is off, unavailable or rejected.",
      keywords: "paste keys ctrl+v cmd+v virtual keyboard ydotool shortcut delivery output" },
    { id: "keepOnClipboard", page: "pasting", section: "Steps", kind: "custom", step: "keep",
      label: stepLabels.keep, icon: "clipboard",
      help: "They stay there after delivery, and that's how you get them when nothing else delivers.",
      offHelp: "After a paste Speecher puts your previous clipboard back, and also when the keys never went out. A paste it can't confirm leaves your words on the clipboard.",
      keywords: "clipboard copy restore previous keep" },
    { id: "outputFormat", page: "pasting", section: "Options", kind: "choice",
      label: "Paste as", help: "Formatted text keeps lists and emphasis in apps that accept it. Inserting through accessibility always writes plain text.",
      keywords: "plain text html rich formatted markdown",
      options: [ { id: "plain", label: "Plain text" }, { id: "html", label: "Formatted" } ] },
    { id: "reviewSelectionEdits", page: "pasting", section: "Options", kind: "toggle",
      label: "Review edits to selected text", help: "When you dictate over a selection, see what will change and press Replace to apply it.",
      keywords: "diff selection edit confirm replace review" },

    { id: "pasteExceptions", page: "pasting", section: "Exceptions", kind: "custom",
      label: "Exceptions",
      help: "Apps that deliver differently from the steps above.",
      keywords: "paste rule app type category per-app application override narrow terminal keys skip paste keys clipboard only "
                + categories.map(c => c.label.toLowerCase()).join(" ") },
    { id: "addPasteException", page: "pasting", section: "Exceptions", kind: "custom",
      label: "Add exception",
      help: "Deliver differently into one app type, or one app named by " + appId.named + ".",
      keywords: "new paste rule app type single app application id bundle program override" },
    // The settings the exceptions are made of. Search finds the list
    // instead (opens).
    { id: "applicationPasteRules", page: "pasting", section: "Exceptions", kind: "custom", opens: "pasteExceptions",
      label: "Rules for single apps", keywords: "" },
    { id: "appRecognitionRules", page: "appRecognition", section: "Application recognition", kind: "custom",
      label: "Application recognition",
      help: "Your matches come first. Each sets the app type for paste rules, the writing profile, or both.",
      keywords: "recognize identify match app type category writing profile built-in" }
].concat(categories.map(function (c) {
    return { id: "categoryPasteRule_" + c.id, page: "pasting", section: "Exceptions", kind: "choice", opens: "pasteExceptions",
             control: "select", label: c.label, keywords: "", options: ruleOptions(false) };
}));
