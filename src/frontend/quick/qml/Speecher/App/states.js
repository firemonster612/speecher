.pragma library

// How a sign-in, health or provider state reads on the app pages, as a
// StatusText tone. States come from Accounts.services, System.checks and the
// providers in Models. Ready states are positive, which StatusText shows
// as plain text; states that need attention take a warning or negative tone.

var tones = {
    signedIn: "positive", ok: "positive", ready: "positive",
    expired: "warning", attention: "warning", signedOut: "warning",
    offline: "warning", noModel: "warning", noRunner: "warning", noServer: "warning", language: "warning",
    unknown: "warning",
    missing: "negative", off: "negative", checkFailed: "negative", disabled: "negative", unusable: "negative"
};

function tone(state) { return tones[state] || "neutral"; }
function busy(state) { return state === "checking" || state === "working"; }

// Service options with the tone ServiceCards colours their status by.
function withTones(options) {
    return options.map(o => Object.assign({}, o, { tone: tone(o.state) }));
}
