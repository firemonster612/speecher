.pragma library

// The profiles themselves live in Library, and the profile for apps Speecher
// doesn't recognize is Other apps' on Apps; these rows give them labels, help
// and search terms.
var rows = [
    { id: "writingProfileBehavior", page: "profiles", section: "Profiles", kind: "custom",
      label: "Profiles",
      help: "Each profile has a cleanup level, a tone and the rules it turns on. An output language translates what you say.",
      keywords: "profile app per-app cleanup level tone rules translate language edit add" },
    { id: "profileCleanup", page: "profiles", section: "Profiles", kind: "custom",
      label: "Cleanup", help: "How much refinement changes your words.",
      keywords: "cleanup level strength none light medium high polish" },
    { id: "profileTone", page: "profiles", section: "Profiles", kind: "custom",
      label: "Tone", help: "The voice the text is written in.",
      keywords: "tone formal casual excited gen z voice style" },
    { id: "profileLanguage", page: "profiles", section: "Profiles", kind: "custom",
      label: "Output language", help: "Writes in this language, whatever you speak. Translating uses at least Light cleanup.",
      placeholder: "Same as spoken", keywords: "translate translation language spanish french output" },
    { id: "profileRules", page: "profiles", section: "Profiles", kind: "custom",
      label: "Rules", help: "What refinement does for this profile. The cleanup level decides which built-in rules start on.",
      keywords: "rules filler paragraphs lists headings digits numbers markdown em dash british spelling lowercase coding agent prompt instructions" },
    { id: "profileServices", page: "profiles", section: "Profiles", kind: "custom",
      label: "Services for this profile",
      help: "Pick a speech or refinement service for this profile only. Each falls back to the ones set for every profile.",
      keywords: "speech service refinement provider model override per profile" },
    { id: "profileApps", page: "profiles", section: "Profiles", kind: "custom",
      label: "Used in", help: "Apps and app types pick their profile on Apps.",
      keywords: "apps applications app type" },

    { id: "yourRules", page: "profiles", section: "Your rules", kind: "custom",
      label: "Your rules", help: "Instructions you write once and turn on in any profile.",
      keywords: "custom rules instructions add new prompt every profile global tone level" }
];
