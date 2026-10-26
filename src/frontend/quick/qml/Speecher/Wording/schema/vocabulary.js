.pragma library

// The lists themselves live in Library; these rows give them labels, help and
// search terms.
var rows = [
    { id: "vocabularyEntries", page: "vocabulary", section: "Terms", kind: "custom",
      label: "Terms",
      help: "Names and words to spell your way. Refinement uses the terms for the dictation's profile; a term marked as a key term also goes to the speech service as a hint.",
      keywords: "words names spelling jargon dictionary custom vocabulary add import csv" },
    { id: "vocabularyKeyTerm", page: "vocabulary", section: "Terms", kind: "custom",
      label: "Key term", help: "Sent to the speech service as a hint, so it hears the term. Refinement uses every term either way.",
      keywords: "hint speech service keyterm boost" },
    { id: "vocabularyContext", page: "vocabulary", section: "Terms", kind: "custom",
      label: "Context", help: "Refinement reads this to decide when the words you said mean this term.",
      placeholder: "The container platform, when I talk about clusters or deploys",
      keywords: "meaning when applies" },
    { id: "vocabularyProfiles", page: "vocabulary", section: "Terms", kind: "custom",
      label: "Profiles", help: "Under any other profile, neither refinement nor the speech service gets this term.",
      keywords: "writing profile only these" },
    { id: "vocabularyLimit", page: "vocabulary", section: "Terms", kind: "custom",
      label: "Limit", help: "Claude takes up to 100 key terms; refinement uses the first 1000 terms.",
      keywords: "limit maximum count how many" },

    { id: "correctionLearningControl", page: "vocabulary", section: "Learned corrections", kind: "toggle",
      label: "Learn corrections",
      help: "After pasting, Speecher briefly watches for your edits and learns corrections you repeat.",
      value: true, keywords: "learn automatic fix edits watch" },
    { id: "learnedCorrections", page: "vocabulary", section: "Learned corrections", kind: "custom",
      label: "Learned corrections", help: "Turn a correction off to stop using it.",
      keywords: "corrections fixes heard corrected undo confidence" },

    { id: "bindingRules", page: "vocabulary", section: "Replacements & snippets", kind: "custom",
      label: "Replacements & snippets",
      help: "Say a phrase, get exact text, including several lines. {date} and {time} become today's date and the time.",
      keywords: "replacement snippet text expansion shortcut phrase template date time import json" }
];
