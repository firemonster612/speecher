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
// is no check. grant marks a permission the system gives; repair says how
// to get a missing tool that has no check to fix it.
var needs = {
    accessibility: mac ? { label: "Accessibility", check: "desktopAccessibility", ready: "Granted", notReady: "Not granted",
                           fix: "Open settings", grant: true }
                       : { label: "Desktop accessibility", check: "desktopAccessibility", grant: true },
    virtualKeyboard: { label: "Virtual keyboard", check: "virtualKeyboardHealth" },
    clipboard: { label: "Clipboard helper or wl-copy", ready: "Available", notReady: "Missing",
                 repair: "Install wl-clipboard for wl-copy, or reinstall Speecher to bring back its clipboard helper." },
    clipboardSave: { label: "wl-paste", ready: "Available", notReady: "Missing",
                     repair: "Install wl-clipboard for wl-paste." }
};
// Under the steps where none of them needs anything here.
var noNeeds = windows ? "Windows does all three itself, with no permission to grant." : "";
// A step whose need an earlier step shows already, fix and all.
function sameNeed(need) { return "Needs the same " + needs[need].label + (needs[need].grant ? " permission" : ""); }
function waitingFor(need) { return "Waiting for " + needs[need].label; }
// A step's switch, as search shows it read-only.
var switchState = { on: "On", off: "Off" };

// Where dictated words end up, by System.pastingOutcome, as Home, setup
// and the Pasting page say it.
var outcomes = { delivers: "Works", clipboard: "Your words stay on the clipboard", nothing: "Nothing can deliver your words" };

// The line under each step's help about how it works here. Wayland's
// clipboard tools come up only in a Wayland session.
function details(step, wayland) {
    switch (step) {
    case "insert":
        return mac ? "Needs Accessibility under Privacy & Security."
             : windows ? "Fields that don't accept it get the paste keys instead." : "";
    case "press":
        return mac ? "Needs the same Accessibility permission as inserting."
             : windows ? "Windows presses the keys for Speecher."
             : "Speecher's virtual keyboard presses the keys." + (wayland ? " Copying needs Speecher's clipboard helper or wl-copy." : "");
    case "keep":
        return linux && wayland ? "This needs Speecher's clipboard helper or wl-copy, and with it off, wl-paste to save your clipboard first." : "";
    }
    return "";
}

// The row for each step, by step id.
var rowOf = { insert: "insertThroughAccessibility", press: "pressPasteKeys", keep: "keepOnClipboard" };
var stepLabels = { insert: "Insert through accessibility", press: "Press the paste keys", keep: "Keep the words on the clipboard" };
var stepShort = { insert: "Insert", press: "Press", keep: "Keep" };

// What a Pasting choice does with the steps here, from
// System.rulePlan(method), as Apps' editor and Pasting's summary say it: the
// steps it leaves that can run on this computer, in order ("Insert, else
// press Ctrl+V, keep a copy"), then those that can't yet and an off step it
// relies on; "No delivery: …" where nothing it allows can run.
// terminalWins marks an App Type whose apps in a terminal paste like
// Terminals instead.
function ruleResult(plan, method, terminalWins) {
    const route = plan.route || [];
    const usable = plan.usable || [];
    const offStep = plan.needs ? stepShort[plan.needs] + " is off" : "";
    if (!route.length) return "No delivery: " + (offStep || "it leaves no step on");
    if (!usable.length) return "No delivery: nothing it allows can run here";
    const keys = method === "terminal_paste" ? terminalKeys : pasteKeys;
    const parts = [];
    if (usable.includes("insert")) parts.push("Insert");
    if (usable.includes("press")) parts.push((parts.length ? "else press " : "Press ") + keys);
    if (usable.includes("keep")) parts.push(parts.length ? "keep a copy" : "Clipboard only");
    const waiting = route.filter(id => !usable.includes(id)).map(id => stepShort[id]);
    return [parts.join(", "),
            waiting.length ? waiting.join(" and ") + " can't run here yet" : "",
            offStep,
            terminalWins ? "Apps in a terminal paste like Terminals" : ""].filter(s => s).join(". ");
}

// Over the apps that paste differently while accessibility is off, by
// whether Speecher has another way to tell apps apart; Insert's row carries
// the fix.
function appsNotice(identifiesApps) {
    const access = mac ? "Accessibility" : "desktop accessibility";
    return identifiesApps ? "Without " + access + ", app detection may be limited, so some of these may not apply (see Insert above)."
                          : "These need " + access + " to tell apps apart (see Insert above).";
}

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

    { id: "appsPasteDifferently", page: "pasting", section: "Apps that paste differently", kind: "custom",
      label: "Apps that paste differently",
      help: "Apps and app types whose way of pasting differs from the steps above. Change them in Apps.",
      keywords: "exception paste rule app type category per-app application override terminal keys skip paste keys clipboard only" }
];
