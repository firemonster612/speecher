.pragma library

// The setup assistant's wording: its steps in order, as they run on Linux,
// and the words around them.

var steps = [
    { id: "welcome", title: "Welcome",
      description: "Speecher records a short dictation, turns it into text and sends it to the app you were using. This assistant checks everything dictation needs." },
    { id: "transcription", title: "Speech to text",
      description: "Choose the service that turns your speech into text." },
    { id: "microphone", title: "Microphone",
      description: "Choose the microphone Speecher should record, then speak to check that the level moves." },
    { id: "accessibility", title: "Accessibility",
      description: "Speecher pastes into the app you are using and reads the text around your cursor so cleanup understands the context. On Linux both work through the desktop accessibility service, which also lets Speecher learn your corrections." },
    { id: "delivery", title: "Pasting",
      description: "To type for you, Speecher installs a small virtual keyboard. Speecher itself never runs privileged." },
    { id: "refinement", title: "Refinement",
      description: "Refinement cleans up what you said: punctuation, filler words and the formatting each app expects. Choose a provider, or skip cleanup." },
    { id: "shortcut", title: "Shortcut",
      description: "Choose what starts dictation: a key combination, or one key on its own, such as Right Alt. Then choose what pressing it does." },
    { id: "ready", title: "Ready to dictate", description: "" }
];

var text = {
    title: "Set up Speecher",
    runAgain: "You can run setup again any time from General › Permissions & health.",
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
    clipboardInstead: "Copy to the clipboard instead",
    clipboardInsteadHelp: "Skip the virtual keyboard. Speecher copies your words and you paste them yourself.",
    clipboardOnly: "Clipboard only",
    cancelShortcutHelp: "Optional. Throws away the dictation in progress.",
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
