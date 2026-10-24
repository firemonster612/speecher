.pragma library

// Ids are core's paste methods; inherit is no rule of the row's own.
var pasteMethods = [
    { id: "standard_paste", label: "Standard paste", help: "Presses Ctrl+V.", icon: "clipboard-paste" },
    { id: "terminal_paste", label: "Terminal paste", help: "Presses Ctrl+Shift+V.", icon: "square-terminal" },
    { id: "direct_insert", label: "Direct insertion", help: "Types into the field through desktop accessibility.", icon: "text-cursor-input" },
    { id: "clipboard_only", label: "Clipboard only", help: "Copies the text for you to paste yourself.", icon: "clipboard" }
];
var inherit = { id: "inherit", label: "Default paste", help: "Whatever Default paste above says." };

// The app types Speecher sorts apps into, each with its paste rule's
// default; ids are core's category names, and host/QuickSettings saves a
// rule for each. Library and the Pasting page read them from here.
var categories = [
    { id: "terminal", label: "Terminals", icon: "square-terminal", examples: "Konsole, Ghostty, Kitty", method: "terminal_paste" },
    { id: "browser", label: "Browsers", icon: "globe", examples: "Firefox, Chrome, Helium" },
    { id: "email", label: "Email apps", icon: "mail", examples: "Thunderbird, KMail" },
    { id: "office", label: "Office apps", icon: "file-text", examples: "LibreOffice Writer" },
    { id: "code_editor", label: "Code editors", icon: "code", examples: "Kate, VS Code, JetBrains" },
    { id: "ai_coding", label: "AI coding apps", icon: "bot", examples: "T3 Code, Cursor, Claude Code" },
    { id: "general", label: "Other apps", icon: "app-window", examples: "Everything else" }
];

var rows = [
    { id: "outputMethod", page: "pasting", section: "Delivery", kind: "choice", control: "cards",
      label: "Paste with",
      help: "Default paste and the rules below can still limit it to inserting or copying.",
      value: "automatic", keywords: "delivery output method ydotool automatic insert type",
      options: [
          { id: "automatic", label: "Automatic", icon: "route", help: "Inserts directly where it can, then presses the paste keys." },
          { id: "ydotool", label: "Virtual keyboard only", icon: "keyboard", help: "Always presses the paste keys with the virtual keyboard." }
      ] },
    { id: "virtualKeyboard", page: "pasting", section: "Delivery", kind: "custom",
      label: "Virtual keyboard", help: "Lets Speecher press the paste keys in other apps.",
      keywords: "ydotool uinput service setup permission" },
    { id: "globalPasteRule", page: "pasting", section: "Delivery", kind: "choice", control: "select",
      label: "Default paste", help: "How Speecher pastes unless a rule below says otherwise.",
      value: "standard_paste", keywords: "paste method ctrl+v terminal direct insertion clipboard only default",
      options: pasteMethods },
    { id: "outputFormat", page: "pasting", section: "Delivery", kind: "choice",
      label: "Paste as", help: "Formatted text keeps lists and emphasis in apps that accept it.",
      value: "plain", keywords: "plain text html rich formatted markdown",
      options: [ { id: "plain", label: "Plain text" }, { id: "html", label: "Formatted" } ] },
    { id: "restoreClipboardAfterTyping", page: "pasting", section: "Delivery", kind: "toggle",
      label: "Restore the clipboard after pasting",
      help: "If Speecher can't confirm the paste, your dictation stays on the clipboard.",
      value: false, keywords: "clipboard restore keep previous copy" },

    { id: "applicationPasteRules", page: "pasting", section: "Rules for single apps", kind: "custom",
      label: "Rules for single apps",
      help: "Paste differently into one app, named by its exact desktop ID, such as org.kde.konsole.",
      value: [], keywords: "app specific per-app paste rule application id override" },
    { id: "appRecognitionRules", page: "pasting", section: "Application recognition", kind: "custom",
      label: "Application recognition",
      help: "Your matches come first. Each sets the app type for paste rules, the Writing Profile, or both.",
      keywords: "recognize identify match app type category writing profile built-in" }
].concat(categories.map(function (c) {
    return { id: "categoryPasteRule_" + c.id, page: "pasting", section: "Rules by app type", kind: "choice",
             control: "select", label: c.label, value: c.method || "inherit",
             keywords: "paste rule app type category " + c.label.toLowerCase(),
             options: [inherit].concat(pasteMethods) };
}));
