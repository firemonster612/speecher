.pragma library

// The profiles themselves, tones, cleanup levels and the fallback profile live
// in Library; these rows give them labels, help and search terms.
var rows = [
    { id: "writingProfileBehavior", page: "profiles", section: "Profiles", kind: "custom",
      label: "Profiles",
      help: "Each profile has a cleanup level, a tone and optional instructions. An output language translates what you say.",
      keywords: "profile app per-app cleanup level tone instructions translate language edit add" },
    { id: "profileCleanup", page: "profiles", section: "Profiles", kind: "custom",
      label: "Cleanup", help: "How much refinement changes your words.",
      keywords: "cleanup level strength none light medium high polish" },
    { id: "profileTone", page: "profiles", section: "Profiles", kind: "custom",
      label: "Tone", help: "The voice the text is written in.",
      keywords: "tone formal casual excited gen z voice style" },
    { id: "profileLanguage", page: "profiles", section: "Profiles", kind: "custom",
      label: "Output language", help: "Writes in this language, whatever you speak. Translating uses at least Light cleanup.",
      placeholder: "Same as spoken, or type a language", keywords: "translate translation language spanish french output" },
    { id: "profileInstructions", page: "profiles", section: "Profiles", kind: "custom",
      label: "Instructions", help: "Added after the instructions for every profile.",
      placeholder: "Keep it short and sign off with my first name.", keywords: "instructions rules custom prompt" },
    { id: "profileServices", page: "profiles", section: "Profiles", kind: "custom",
      label: "Services for this profile",
      help: "Pick a speech service or refinement provider for this profile only. Each falls back to the ones set for every profile.",
      keywords: "speech service refinement provider model override per profile" },
    { id: "profileApps", page: "profiles", section: "Profiles", kind: "custom",
      label: "Used in", help: "Apps are matched to profiles in Application recognition, under Pasting › App rules.",
      keywords: "apps applications match recognition" },
    { id: "defaultWritingProfile", page: "profiles", section: "Every profile", kind: "custom",
      label: "When the app isn't recognized", help: "The profile for every app no match names.",
      value: "other", keywords: "fallback default profile unknown app unrecognized" },

    { id: "additionalInstructions", page: "profiles", section: "Every profile", kind: "custom",
      label: "Instructions for every profile",
      help: "Added to every refinement, before each profile's own instructions.",
      placeholder: "Write numbers as digits.", keywords: "global instructions all profiles rules" },
    { id: "customTones", page: "profiles", section: "Every profile", kind: "custom",
      label: "Your tones", help: "A voice you can pick in any profile.",
      keywords: "custom tone add new voice style" },
    { id: "customCleanupLevels", page: "profiles", section: "Every profile", kind: "custom",
      label: "Your cleanup levels", help: "Light, Medium or High plus your own rules.",
      keywords: "custom cleanup level add new based on" }
];
