.pragma library

var os = Qt.platform.os;
var mac = os === "osx";
var windows = os === "windows";
var linux = !mac && !windows;

// The paste keys, as each platform presses them.
var pasteKeys = mac ? "⌘V" : "Ctrl+V";

// What each delivery step needs on this computer, as its row on Pasting
// shows it: check is the System check it reads (with that check's fix),
// ready and notReady the status words where the check's own don't fit, and
// detail the line under the step's help. A step with no check needs nothing
// of the person here.
var needs = {
    insert: linux ? { check: "desktopAccessibility", label: "Desktop accessibility",
                      detail: "On Linux it can't replace selected text yet, so edits to a selection need the paste keys." }
          : mac ? { check: "desktopAccessibility", label: "Accessibility", ready: "Granted", notReady: "Not granted",
                    fix: "Open settings", detail: "Needs Accessibility under Privacy & Security." }
          : { label: "UI Automation", ready: "No permission needed",
              detail: "Fields that don't accept it get the paste keys instead." },
    press: linux ? { check: "virtualKeyboardHealth", label: "Virtual keyboard",
                     detail: "Speecher's virtual keyboard presses the keys." }
         : mac ? { check: "desktopAccessibility", label: "Accessibility", ready: "Granted", notReady: "Not granted",
                   fix: "Open settings", detail: "Needs the same Accessibility permission as inserting." }
         : { label: "SendInput", ready: "No permission needed", detail: "Windows presses the keys for Speecher." },
    keep: linux ? { label: "Clipboard", ready: "Available", notReady: "Needs wl-copy",
                    detail: "On Wayland this needs Speecher's clipboard helper or wl-copy." }
        : { label: "Clipboard", ready: "Always available" }
};

// Rule options, worded as what they do to the steps above. Ids are core's
// paste methods. The first is the row's own "no override": inherit (no rule
// of its own) for an app type, standard_paste for a single app's rule.
// Terminals press ⌘V on macOS too, so it has no Terminal keys.
function ruleOptions(sameId) {
    return [
        { id: sameId, label: "Same as above", help: "Takes the steps above as they are.", icon: "list-checks" },
        { id: "terminal_paste", label: "Terminal keys", help: "The steps above, pressing Ctrl+Shift+V.", icon: "square-terminal",
          step: "press" },
        { id: "direct_insert", label: "Insert only", help: "Inserts through accessibility and skips the paste keys.",
          icon: "text-cursor-input", step: "insert" },
        { id: "clipboard_only", label: "Clipboard only", help: "Leaves the words on the clipboard for you to paste.",
          icon: "clipboard", step: "keep" }
    ].filter(o => o.id !== "terminal_paste" || !mac);
}

// The app types Speecher sorts apps into; ids are core's category names, and
// host/QuickSettings saves a rule for each. Library and the Pasting page
// read them from here.
var categories = [
    { id: "terminal", label: "Terminals", icon: "square-terminal", examples: "Konsole, Ghostty, Kitty" },
    { id: "browser", label: "Browsers", icon: "globe", examples: "Firefox, Chrome, Helium" },
    { id: "email", label: "Email apps", icon: "mail", examples: "Thunderbird, KMail" },
    { id: "office", label: "Office apps", icon: "file-text", examples: "LibreOffice Writer" },
    { id: "code_editor", label: "Code editors", icon: "code", examples: "Kate, VS Code, JetBrains" },
    { id: "ai_coding", label: "AI coding apps", icon: "bot", examples: "Cursor, Claude Code" },
    { id: "general", label: "Other apps", icon: "app-window", examples: "Everything else" }
];

// How a single app is named in a rule, as each platform's target reports it.
var appId = linux ? { label: "Desktop ID", named: "its desktop ID", example: "org.kde.konsole",
                      help: "The app's desktop ID, such as org.kde.konsole." }
          : mac ? { label: "Bundle ID", named: "its bundle ID", example: "com.apple.Terminal",
                    help: "The app's bundle ID, such as com.apple.Terminal." }
          : { label: "Program name", named: "its program name", example: "windowsterminal",
              help: "The program's file name without .exe, such as windowsterminal." };

// The row for each step, by step id, and what a rule that relies on an off
// step says on its row: "Needs Press the paste keys, which is off."
var rowOf = { insert: "insertThroughAccessibility", press: "pressPasteKeys", keep: "keepOnClipboard" };
var stepLabels = { insert: "Insert through accessibility", press: "Press the paste keys", keep: "Keep the words on the clipboard" };
function needsStep(step) { return "Needs " + stepLabels[step] + ", which is off."; }

// The step rows are custom: the Pasting page draws each one's switch with
// what it needs, and keeps the last one on.
var rows = [
    { id: "insertThroughAccessibility", page: "pasting", section: "Steps", kind: "custom", step: "insert",
      label: stepLabels.insert, icon: "text-cursor-input",
      help: "Writes your words straight into the field you're in.",
      keywords: "direct insertion type accessibility at-spi ui automation delivery output", need: needs.insert },
    { id: "pressPasteKeys", page: "pasting", section: "Steps", kind: "custom", step: "press",
      label: stepLabels.press, icon: "keyboard",
      help: "Copies your words and presses " + pasteKeys + " when insertion is off, unavailable or rejected.",
      keywords: "paste keys ctrl+v cmd+v virtual keyboard ydotool shortcut delivery output", need: needs.press },
    { id: "keepOnClipboard", page: "pasting", section: "Steps", kind: "custom", step: "keep",
      label: stepLabels.keep, icon: "clipboard",
      help: "They stay there after delivery, and that's how you get them when nothing else delivers.",
      offHelp: "Speecher puts your previous clipboard back after a confirmed paste and leaves no copy of its own.",
      keywords: "clipboard copy restore previous keep", need: needs.keep },
    { id: "outputFormat", page: "pasting", section: "Format", kind: "choice",
      label: "Paste as", help: "Formatted text keeps lists and emphasis in apps that accept it. Inserting through accessibility always writes plain text.",
      keywords: "plain text html rich formatted markdown",
      options: [ { id: "plain", label: "Plain text" }, { id: "html", label: "Formatted" } ] },
    { id: "reviewSelectionEdits", page: "pasting", section: "Selection edits", kind: "toggle",
      label: "Review edits to selected text", help: "When you dictate over a selection, see what will change and press Replace to apply it.",
      keywords: "diff selection edit confirm replace review" },

    { id: "applicationPasteRules", page: "pasting", section: "Rules for single apps", kind: "custom",
      label: "Rules for single apps",
      help: "Deliver differently into one app, named by " + appId.named + ".",
      keywords: "app specific per-app paste rule application id bundle program override" },
    { id: "appRecognitionRules", page: "appRecognition", section: "Application recognition", kind: "custom",
      label: "Application recognition",
      help: "Your matches come first. Each sets the app type for paste rules, the writing profile, or both.",
      keywords: "recognize identify match app type category writing profile built-in" }
].concat(categories.map(function (c) {
    return { id: "categoryPasteRule_" + c.id, page: "pasting", section: "Rules by app type", kind: "choice",
             control: "select", label: c.label,
             keywords: "paste rule app type category " + c.label.toLowerCase(),
             options: ruleOptions("inherit") };
}));
