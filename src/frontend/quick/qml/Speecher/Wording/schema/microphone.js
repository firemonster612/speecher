.pragma library

// Rows for the microphone page. The input device, the test and the system
// settings button are custom: their options, level and action come from the
// Audio singleton.
var rows = [
    { id: "audioDevice", page: "microphone", section: "Input", kind: "custom",
      label: "Input device", help: "The microphone Speecher records from.", value: "",
      disabledHelp: "Speecher can't see a microphone. Plug one in or check that your sound server allows access.",
      disabledActionLabel: "Check again",
      missingHelp: "This saved microphone is not currently available.",
      missingActionLabel: "Use system default",
      keywords: "microphone input device source headset usb default missing" },
    { id: "microphoneTest", page: "microphone", section: "Input", kind: "custom",
      label: "Test microphone", help: "Speak and watch the level to check that the microphone hears you.",
      keywords: "level meter check volume hear input test" },
    { id: "openMicrophoneSettings", page: "microphone", section: "Input", kind: "custom",
      label: "Microphone settings", help: "Your system's input volume and microphone permissions.",
      actionLabel: "Open microphone settings", actionIcon: "external-link",
      keywords: "system sound settings permission privacy volume" },

    { id: "captureMode", page: "microphone", section: "Opening the microphone", kind: "choice",
      label: "Keep microphone open",
      help: "Keeping it open starts dictation a little faster, but the microphone shows as in use the whole time.",
      value: "on_demand", keywords: "warm always open capture mode latency in use indicator",
      options: [
          { id: "on_demand", label: "Only while dictating" },
          { id: "warm", label: "Between dictations too" }
      ] },
    { id: "readinessTimeoutMs", page: "microphone", section: "Opening the microphone", kind: "slider",
      label: "Wait for microphone",
      help: "How long to wait for the microphone to deliver sound before giving up.",
      value: 900, min: 500, max: 3000, step: 50, unit: "ms", keywords: "timeout readiness slow bluetooth" },

    { id: "preRollMs", page: "microphone", section: "Recording", kind: "slider",
      label: "Keep before speech",
      help: "Audio kept from just before you start, so the first word is not clipped.",
      value: 250, min: 0, max: 1500, step: 50, unit: "ms", keywords: "pre-roll clipped first word start buffer" },
    { id: "postRollMs", page: "microphone", section: "Recording", kind: "slider",
      label: "Keep after stopping",
      help: "Audio kept after you stop or go quiet, so the last word is not clipped.",
      value: 200, min: 0, max: 1500, step: 50, unit: "ms", keywords: "post-roll clipped last word end tail" },
    { id: "vadEnabled", page: "microphone", section: "Recording", kind: "toggle",
      label: "Skip silence", help: "Leaves quiet stretches out of what is transcribed.",
      value: false, keywords: "vad voice activity detection quiet pauses trim" },
    { id: "vadThresholdPercent", page: "microphone", section: "Recording", kind: "slider",
      label: "Quiet below",
      help: "Raise this if background noise is being kept; lower it if soft speech is being cut.",
      value: 2, min: 1, max: 20, step: 1, unit: "%", enabledWhen: { id: "vadEnabled", equals: true },
      keywords: "vad threshold silence noise sensitivity" }
];
