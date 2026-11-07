.pragma library

// The setup assistant's wording: its steps in order, and the words around
// them. Pasting comes before Accessibility, so the assistant asks only for
// what the chosen delivery steps need; Windows needs no Accessibility step.

var os = Qt.platform.os;

var steps = [
    { id: "welcome", title: "Welcome",
      description: "Speecher records a short dictation, turns it into text and sends it to the app you were using. This assistant checks everything dictation needs." },
    { id: "transcription", title: "Speech to text",
      description: "Choose the service that turns your speech into text, and the language you speak." },
    { id: "microphone", title: "Microphone",
      description: "Choose the microphone Speecher should record, then speak to check that the level moves." },
    { id: "delivery", title: "Pasting",
      description: "Choose how your words get into the app you're using. Speecher tries these in order, and asks only for what the ones you keep need." },
    { id: "accessibility", title: "Accessibility",
      description: os === "osx"
          ? "Accessibility lets Speecher insert your words, press ⌘V for you, read the text around your cursor so cleanup understands it, and tell apps apart. Without it, your words wait on the clipboard for you to paste."
          : "The desktop accessibility service lets Speecher insert your words, tell apps apart, read the text around your cursor so cleanup understands it, and learn your corrections." },
    { id: "refinement", title: "Refinement",
      description: "Refinement cleans up what you said: punctuation, filler words and the formatting each app expects. Choose a service, or skip cleanup." },
    { id: "shortcut", title: "Shortcut",
      description: "Choose what starts dictation: a key combination, or one key on its own, such as Right Alt. Then choose what pressing it does." },
    { id: "ready", title: "Ready to dictate", description: "" }
];

var text = {
    title: "Set up Speecher",
    runAgain: "You can run setup again any time from Home or General.",
    skip: "Skip setup",
    back: "Back",
    getStarted: "Get started",
    continue_: "Continue",
    waits: "Continue opens once this step passes its check.",
    // The last step's button closes setup; it starts nothing.
    finish: "Finish setup",
    needs: "Dictation needs these:",
    allPassed: "Every step passed its check.",
    almostReady: "Almost ready",
    notReady: "Not ready to dictate yet",
    worksWithout: "Dictation works without these, but each makes it better:",
    notChosen: "Not chosen",
    welcomeTitle: "Welcome to Speecher",
    welcomeHint: "Once setup is done, press it in any app, speak, and Speecher types what you said.",
    welcomeHintNoKeys: "Once setup is done, press your dictation shortcut in any app, speak, and Speecher types what you said.",
    welcomeHintCommand: "Once setup is done, run this from a terminal or your own keybinding, speak, and Speecher types what you said.",
    signInsFound: "Sign-ins found on this computer",
    noSignIns: "No sign-ins found yet. The next step helps you choose a service.",
    downloadModel: "Download a model to continue",
    downloadModelHelp: "The download keeps going while you finish setup.",
    optional: "Optional.",
    cleanupNotReady: "Cleanup isn't ready",
    cleanupNotReadyHelp: "Fix it above to continue, or skip cleanup and Speecher pastes your words as spoken.",
    skipCleanup: "Skip cleanup",
    // The Pasting step's verdict, by the steps on, and the Accessibility
    // step's where those steps don't need it.
    stepNames: { insert: "Insert", press: "Paste keys", keep: "Clipboard" },
    notNeeded: "Not needed for your steps",
    commandOnly: "The speecher command"
};

// The cases where a check that does not pass still lets its step go on,
// since this computer gets no further: accessibility it can't have or has
// only until sign-out, and a keyboard waiting for the next sign-in. The
// check keeps saying so.
var settles = {
    desktopAccessibility: ["unsupported", "sessionOnly"],
    virtualKeyboardHealth: ["needsSignOut"]
};

// Whether the check lets its setup step go on.
function settled(check) { return check.state === "ok" || (settles[check.id] || []).includes(check.reason); }

// Ready's heading over the steps still open: "2 steps need attention".
function stepsOpen(count) { return count === 1 ? "One step needs attention" : count + " steps need attention"; }
