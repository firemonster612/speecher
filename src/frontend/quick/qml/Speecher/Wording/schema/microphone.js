.pragma library

// Rows for the microphone page. The input device and the test are custom:
// their microphones, level and state come from the Audio singleton. The
// timing rows sit in the page's Advanced group, which a search for one opens.
var os = Qt.platform.os;
var rows = [
    { id: "audioDevice", page: "microphone", section: "Input", kind: "custom",
      label: "Input device", help: "The microphone Speecher records from.",
      disabledHelp: os === "windows" ? "Speecher can't see a microphone. Plug one in, or check that it is turned on in Windows' Sound settings."
                  : os === "osx" ? "Speecher can't see a microphone. Plug one in, or check System Settings › Sound › Input."
                  : "Speecher can't see a microphone. Plug one in, or check that your sound server allows access.",
      disabledActionLabel: "Check again",
      defaultLabel: "System default", defaultHelp: "%1 right now",
      noneLabel: "No microphone found", noneHelp: "Connect or enable an input device, then try again.",
      missingLabel: "Missing microphone",
      missingHelp: "This saved microphone is not currently available.",
      missingActionLabel: "Use system default",
      keywords: "microphone input device source headset usb default missing" },
    { id: "microphoneTest", page: "microphone", section: "Input", kind: "custom",
      label: "Test microphone", help: "Speak and watch the level to check that the microphone hears you.",
      startLabel: "Start test", startingLabel: "Starting…", stopLabel: "Stop test", retryLabel: "Try again",
      listeningText: "Listening to %1", noMicrophoneText: "No microphone found",
      accessOffText: "Microphone access is off.",
      heardText: "Speecher hears your microphone.", notHeardText: "Not heard yet",
      noMicrophoneHint: "Plug in or turn on a microphone. It shows up here as soon as your computer sees it.",
      keywords: "level meter check volume hear input test" },

    { id: "captureMode", page: "microphone", section: "Advanced", kind: "choice",
      label: "Keep microphone open",
      help: "Keeping it open starts dictation a little faster, but the microphone shows as in use the whole time.",
      keywords: "warm always open capture mode latency in use indicator",
      options: [
          { id: "on_demand", label: "Only while dictating" },
          { id: "warm", label: "Between dictations too" }
      ] },
    { id: "readinessTimeoutMs", page: "microphone", section: "Advanced", kind: "slider",
      label: "Wait for microphone",
      help: "How long to wait for the microphone to deliver sound before giving up.",
      min: 500, max: 3000, step: 50, unit: "ms", keywords: "timeout readiness slow bluetooth" },

    { id: "preRollMs", page: "microphone", section: "Advanced", kind: "slider",
      label: "Audio before you speak",
      help: "Audio taken from just before you start, so the first word is not clipped.",
      min: 0, max: 1500, step: 50, unit: "ms", keywords: "pre-roll clipped first word start buffer" },
    { id: "postRollMs", page: "microphone", section: "Advanced", kind: "slider",
      label: "Audio after you stop",
      help: "Audio taken after you stop or go quiet, so the last word is not clipped.",
      min: 0, max: 1500, step: 50, unit: "ms", keywords: "post-roll clipped last word end tail" },
    { id: "vadEnabled", page: "microphone", section: "Advanced", kind: "toggle",
      label: "Skip silence", help: "Leaves quiet stretches out of what is transcribed.",
      keywords: "vad voice activity detection quiet pauses trim" },
    { id: "vadThresholdPercent", page: "microphone", section: "Advanced", kind: "slider",
      label: "Quiet below",
      help: "Raise this if background noise gets transcribed or keeps a recording going; lower it if soft speech is being cut.",
      // Recordings' Stop after silence times silence by it too.
      min: 1, max: 20, step: 1, unit: "%",
      enabledWhen: [{ id: "vadEnabled", equals: true }, { id: "recordingAutoStop", notEquals: "0" }],
      disabledHelp: "Turn on Skip silence, or Stop after silence on Recordings, to set what counts as quiet.",
      keywords: "vad threshold silence noise sensitivity" }
];
