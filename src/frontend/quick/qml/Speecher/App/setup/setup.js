.pragma library

// The setup assistant's wording: its steps in order, and the words around
// them. The steps show the pages' own sections, which word themselves.

var steps = [
    { id: "welcome", title: "Welcome",
      description: "Speecher records a short dictation, turns it into text and sends it to the app you were using. This assistant checks everything dictation needs." },
    { id: "transcription", title: "Speech to text",
      description: "Choose the language you speak, then the services that turn it into text, first to last." },
    { id: "microphone", title: "Microphone",
      description: "Speak to check that Speecher hears you. If the level stays flat, choose another microphone below." },
    { id: "delivery", title: "Pasting",
      description: "Choose how your words get into the app you're using." },
    { id: "refinement", title: "Refinement",
      description: "Refinement cleans up what you said: punctuation, filler words and the formatting each app expects. Turn it off to paste your words as spoken." },
    { id: "shortcut", title: "Shortcut",
      description: "Choose what starts dictation: a key combination, or one key on its own, such as Right Alt. Then choose what pressing it does." },
    { id: "ready", title: "Ready to dictate", description: "" }
];

var text = {
    title: "Set up Speecher",
    runAgain: "You can run setup again any time from Home or General.",
    skip: "Skip setup",
    // Saves what was chosen and closes; setup opens again next time.
    later: "Set up later",
    back: "Back",
    getStarted: "Get started",
    continue_: "Continue",
    // The last step's button closes setup; it starts nothing. While a step
    // dictation needs is open it saves what was chosen, and setup opens
    // again next time, which the line beside it says.
    finish: "Finish setup",
    finishForNow: "Finish for now",
    stillMissing: titles => (titles.length > 1 ? titles.slice(0, -1).join(", ") + " and " + titles[titles.length - 1] + " still need"
                                               : titles[0] + " still needs") + " attention, so setup opens again next time.",
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
    // The Pasting step's verdict once the words get into the app, by the
    // steps on; otherwise Schema.pasting.outcomes says where they end up.
    stepNames: { insert: "Insert", press: "Paste keys", keep: "Clipboard" },
    commandOnly: "The speecher command",
    // Ready, where the desktop gives no shortcut.
    startWithCommand: "Run this from a terminal or your own keybinding to dictate.",
    takesOver: label => label + " takes over"
};

// Ready's heading over the steps still open: "2 steps need attention".
function stepsOpen(count) { return count === 1 ? "One step needs attention" : count + " steps need attention"; }
