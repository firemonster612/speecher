.pragma library

var os = Qt.platform.os;
var mac = os === "osx";
var windows = os === "windows";
var linux = !mac && !windows;

// The paste keys, as each platform presses them, and a terminal's. An App
// or App Type picks between them by id (core's PasteKeyCombo names); macOS
// presses ⌘V everywhere, so it has no choice.
var pasteKeys = mac ? "⌘V" : "Ctrl+V";
var terminalKeys = "Ctrl+Shift+V";
var keyChoices = mac ? [] : [
    { id: "standard", label: pasteKeys, icon: "keyboard" },
    { id: "terminal", label: terminalKeys, icon: "square-terminal" }
];
function keysLabel(id) { return id === "terminal" && !mac ? terminalKeys : pasteKeys }

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
// The needs a step waits for, by id, as one sentence without its stop.
function waitingFor(ids) {
    const labels = [].concat(ids).map(id => needs[id].label);
    return "Waiting for " + (labels.length > 1 ? labels.slice(0, -1).join(", ") + " and " + labels[labels.length - 1] : labels[0]);
}
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

// The row for each step, by step id, and the one short name each step has
// everywhere: the Pasting page, its picture, Apps and search.
var rowOf = { insert: "insertThroughAccessibility", press: "pressPasteKeys", keep: "keepOnClipboard" };
var stepIds = ["insert", "press", "keep"];
var stepLabels = { insert: "Insert", press: "Press paste keys", keep: "Keep on clipboard" };
var stepShort = { insert: "Insert", press: "Press", keep: "Keep" };

// Steps ({ insert, press, keep }) as a short list: "Insert, Press, Keep".
function stepsText(steps) {
    return stepIds.filter(id => steps && steps[id]).map(id => stepShort[id]).join(", ");
}

// What delivering with some steps does here, from System.stepsPlan(steps)
// (each step {id, on, ready}, in the order delivery tries them), as Apps'
// editor and Pasting's list say it: the steps that can run on this
// computer, in order ("Insert, else press Ctrl+V, keep a copy"), then those
// that can't yet; "No delivery here yet" where none can. keys is the paste
// keys' id, named on a Press that can't run when they're a terminal's.
// terminalWins marks an App Type whose apps in a terminal take Terminals'
// Pasting instead.
function pastingResult(steps, keys, terminalWins) {
    const route = steps.filter(s => s.on).map(s => s.id);
    const usable = steps.filter(s => s.on && s.ready).map(s => s.id);
    const parts = [];
    if (usable.includes("insert")) parts.push("Insert");
    if (usable.includes("press")) parts.push((parts.length ? "else press " : "Press ") + keysLabel(keys));
    if (usable.includes("keep")) parts.push(parts.length ? "keep a copy" : "Clipboard only");
    else if (usable.includes("press")) parts.push("then put your clipboard back");
    // A Press that can't run still names keys of its own, which may be why
    // the app is listed: "Press (Ctrl+Shift+V)".
    const waiting = route.filter(id => !usable.includes(id))
        .map(id => stepShort[id] + (id === "press" && keys === "terminal" && !mac ? " (" + terminalKeys + ")" : ""));
    if (!usable.length)
        return ["No delivery here yet: " + waiting.join(" and ") + " can't run",
                terminalWins ? "Apps in a terminal paste like Terminals" : ""].filter(s => s).join(". ");
    return [parts.join(", "),
            waiting.length ? waiting.join(" and ") + " can't run here yet" : "",
            terminalWins ? "Apps in a terminal paste like Terminals" : ""].filter(s => s).join(". ");
}

// What desktop accessibility turns on, while it is off, as Home's readiness
// row for it says. identifiesApps: Speecher can tell apps apart without it
// (System.identifiesAppsWithoutAccessibility).
function accessibilityNotice(identifiesApps) {
    const lost = (mac ? ["press the paste keys"] : ["insert your words"])
        .concat(identifiesApps ? [] : ["tell which app you're in"])
        .concat(["read the text around the cursor", "learn your corrections"]);
    return (mac ? "Accessibility isn't granted" : "Desktop accessibility is off") + ", so Speecher can't "
        + lost.slice(0, -1).join(", ") + " or " + lost[lost.length - 1] + ".";
}

// The line a card shows while accessibility is off (reason "off") or this
// build has none ("unsupported"): what of the card needs it, then the link
// to Home's row for it, where it can be turned on (linkAbove on Home).
var accessibilityLink = "Turn it on from Home";
var accessibilityLinkAbove = "Turn it on above";
// What each card needs it for, by what the card does.
var accessibilityUses = { learning: "Learning your corrections", context: "Reading the text around the cursor",
                          apps: "Telling which app you're in", inserting: "Inserting your words",
                          profiles: "Picking a profile by app" };
function accessibilityLine(what, reason, link) {
    const state = reason === "unsupported" ? "which this build of Speecher doesn't include."
                : mac ? "which isn't granted. " + link : "which is off. " + link;
    return what + " needs " + needs.accessibility.label + ", " + state;
}

// The step rows are custom: the Pasting page draws each one's switch with
// what it needs, and keeps the last one on.
var rows = [
    { id: "insertThroughAccessibility", page: "pasting", section: "Steps", kind: "custom", step: "insert",
      label: stepLabels.insert, icon: "text-cursor-input",
      help: "Writes your words straight into the field you're in, through accessibility.",
      keywords: "direct insertion type accessibility at-spi ui automation delivery output" },
    { id: "pressPasteKeys", page: "pasting", section: "Steps", kind: "custom", step: "press",
      label: stepLabels.press, icon: "keyboard",
      help: "Puts your words on the clipboard and presses " + pasteKeys + " when insertion is off, unavailable or rejected. "
          + "With Keep off, Speecher puts your clipboard back afterwards, so this step alone pastes and leaves your clipboard as it was.",
      keywords: "paste keys ctrl+v cmd+v virtual keyboard ydotool shortcut delivery output" },
    { id: "keepOnClipboard", page: "pasting", section: "Steps", kind: "custom", step: "keep",
      label: stepLabels.keep, icon: "clipboard",
      help: "Your words stay on the clipboard after delivery, which is how you get them when nothing else delivers.",
      offHelp: "Speecher puts your previous clipboard back after a paste, or when the keys never went out. "
          + "So with Insert off and Press on, it pastes and your clipboard ends up as it was. "
          + "A paste it can't confirm gets a moment to land before your clipboard goes back.",
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
      help: "Apps and app types with steps or paste keys of their own. Change them in Apps.",
      keywords: "exception paste rule app type category per-app application override terminal keys ctrl+shift+v own steps clipboard only" }
];
