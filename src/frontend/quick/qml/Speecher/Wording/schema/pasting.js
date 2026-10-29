.pragma library

// Ids are core's paste methods; inherit is no rule of the row's own.
// macOS pastes with ⌘V everywhere, terminals included.
var mac = Qt.platform.os === "osx";
var pasteMethods = [
    { id: "standard_paste", label: "Standard paste", help: mac ? "Presses ⌘V." : "Presses Ctrl+V.", icon: "clipboard-paste" },
    { id: "terminal_paste", label: "Terminal paste", help: mac ? "Presses ⌘V." : "Presses Ctrl+Shift+V.", icon: "square-terminal" },
    { id: "direct_insert", label: "Direct insertion", help: "Types into the field through desktop accessibility.", icon: "text-cursor-input" },
    { id: "clipboard_only", label: "Clipboard only", help: "Copies the text for you to paste yourself.", icon: "clipboard" }
];
var inherit = { id: "inherit", label: "Default paste", help: "Whatever Default paste on Pasting says." };

// The app types Speecher sorts apps into; ids are core's category names, and
// host/QuickSettings saves a rule for each. Library and the App rules page
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

var rows = [
    { id: "outputMethod", page: "pasting", section: "Delivery", kind: "choice", control: "cards",
      label: "Paste with",
      help: "Default paste and App rules can still limit it to inserting or copying.",
      keywords: "delivery output method ydotool automatic insert type",
      options: [
          { id: "automatic", label: "Automatic", icon: "route", help: "Inserts directly where it can, then presses the paste keys." },
          { id: "ydotool", label: "Virtual keyboard only", icon: "keyboard", help: "Always presses the paste keys with the virtual keyboard.",
            needs: "hasVirtualKeyboard" }
      ] },
    { id: "virtualKeyboard", page: "pasting", section: "Delivery", kind: "custom",
      label: "Virtual keyboard", help: "Lets Speecher press the paste keys in other apps.",
      keywords: "ydotool uinput service setup permission", needs: "hasVirtualKeyboard" },
    { id: "globalPasteRule", page: "pasting", section: "Delivery", kind: "choice", control: "select",
      label: "Default paste", help: "How Speecher pastes unless an app rule says otherwise.",
      keywords: "paste method ctrl+v terminal direct insertion clipboard only default",
      options: pasteMethods },
    { id: "outputFormat", page: "pasting", section: "Delivery", kind: "choice",
      label: "Paste as", help: "Formatted text keeps lists and emphasis in apps that accept it.",
      keywords: "plain text html rich formatted markdown",
      options: [ { id: "plain", label: "Plain text" }, { id: "html", label: "Formatted" } ] },
    { id: "restoreClipboardAfterTyping", page: "pasting", section: "Delivery", kind: "toggle",
      label: "Restore the clipboard after pasting",
      help: "If Speecher can't confirm the paste, your dictation stays on the clipboard.",
      keywords: "clipboard restore keep previous copy" },

    { id: "applicationPasteRules", page: "appRules", section: "Rules for single apps", kind: "custom",
      label: "Rules for single apps",
      help: "Paste differently into one app, named by its exact desktop ID, such as org.kde.konsole.",
      keywords: "app specific per-app paste rule application id override" },
    { id: "appRecognitionRules", page: "appRules", section: "Application recognition", kind: "custom",
      label: "Application recognition",
      help: "Your matches come first. Each sets the app type for paste rules, the writing profile, or both.",
      keywords: "recognize identify match app type category writing profile built-in" }
].concat(categories.map(function (c) {
    return { id: "categoryPasteRule_" + c.id, page: "appRules", section: "Rules by app type", kind: "choice",
             control: "select", label: c.label,
             keywords: "paste rule app type category " + c.label.toLowerCase(),
             options: [inherit].concat(pasteMethods) };
}));
