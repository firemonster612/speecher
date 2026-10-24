.pragma library

// How a sign-in, health or setup state reads on the app pages, as a
// StatusText tone. States come from Accounts.services, System.checks and
// Models.readiness. Ready states are positive, which StatusText shows
// as plain text; states that need attention take a warning or negative tone.

var tones = {
    signedIn: "positive", ok: "positive", ready: "positive",
    expired: "warning", attention: "warning", signedOut: "warning",
    missing: "negative", off: "negative"
};

function tone(state) { return tones[state] || "neutral"; }
function busy(state) { return state === "checking" || state === "working"; }

// Setup's service options with the tone ChoiceRows colours their status by.
function withTones(options) {
    return options.map(o => Object.assign({}, o, { tone: tone(o.state) }));
}
