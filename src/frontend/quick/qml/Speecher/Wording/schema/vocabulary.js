.pragma library

// The Vocabulary page's rows and wording. The list itself lives in Library;
// these rows give it labels, help and search terms, and land a search on
// the list with the filter that shows them.
var rows = [
    { id: "vocabularyEntries", page: "vocabulary", section: "Your vocabulary", kind: "custom",
      label: "Spellings",
      help: "Names and words to spell your way. Key terms go to the speech service as hints, and refinement reads every spelling.",
      keywords: "words names spelling jargon dictionary terms custom vocabulary add import csv" },
    { id: "bindingRules", page: "vocabulary", section: "Your vocabulary", kind: "custom",
      label: "Replacements & snippets",
      help: "Say a phrase, get exact text, including several lines. {date} and {time} become today's date and the time.",
      keywords: "replacement snippet text expansion shortcut phrase template date time import json" },
    { id: "learnedCorrections", page: "vocabulary", section: "Your vocabulary", kind: "custom",
      label: "Learned corrections",
      help: "Replacements Speecher added after you fixed the same words the same way, in the app you fixed them in.",
      keywords: "corrections fixes heard corrected learned undo confidence edits" },
    { id: "vocabularyKeyTerm", page: "vocabulary", section: "Your vocabulary", kind: "custom",
      label: "Key term", help: "Sent to the speech service as a hint, so it hears the term. Refinement uses every spelling either way.",
      keywords: "hint speech service keyterm boost" },
    { id: "vocabularyContext", page: "vocabulary", section: "Your vocabulary", kind: "custom",
      label: "Context", help: "Refinement reads this to decide when the words you said mean this term.",
      placeholder: "The container platform, when I talk about clusters or deploys",
      keywords: "meaning when applies" },
    { id: "vocabularyProfiles", page: "vocabulary", section: "Your vocabulary", kind: "custom",
      label: "Limit to",
      help: "Under any other writing profile, or in any other app, neither the speech service nor refinement gets it.",
      keywords: "writing profile app only these limit scope per-app" },
    { id: "vocabularyLimit", page: "vocabulary", section: "Your vocabulary", kind: "custom",
      label: "Key term limit", help: "Claude takes up to 100 key terms; refinement uses the first 1000 spellings.",
      keywords: "limit maximum count how many" },

    { id: "correctionLearningControl", page: "vocabulary", section: "Learning from your edits", kind: "toggle",
      label: "Learn from my edits",
      help: "After pasting, Speecher briefly watches for your edits and learns corrections you repeat.",
      usableWhen: "accessibility", keywords: "learn automatic fix edits watch corrections" }
];

// What each filter shows, and the row a search for it lands on.
var filters = [
    { id: "all", label: "All", row: "vocabularyEntries" },
    { id: "spelling", label: "Spellings", row: "vocabularyEntries" },
    { id: "replacement", label: "Replacements", row: "bindingRules" },
    { id: "learned", label: "Learned", row: "learnedCorrections" }
];
function filterFor(rowId) { return (filters.find(f => f.row === rowId && f.id !== "all") || {}).id || "" }
function shows(filter, entry) {
    return filter === "spelling" ? entry.kind === "spelling"
         : filter === "replacement" ? entry.kind === "replacement" && !entry.learned
         : filter === "learned" ? entry.learned
         : true
}

// The page's words: its list, editor, import and learning.
var t = {
    yourVocabulary: "Your vocabulary",
    add: "Add",
    import_: "Import",
    importTitle: "Import spellings from CSV, or snippets from JSON",
    importFiles: ["Vocabulary files (*.csv *.json)", "CSV spellings (*.csv)", "JSON snippets (*.json)", "All files (*)"],
    nothingNew: "That file has nothing you don't have.",
    search: "Search",
    limitedTo: "Limited to",
    anyLimit: "Any limit",
    newEntry: "New entry",
    editEntry: "Edit entry",
    whenYouSay: "When you say",
    write: "Write instead",
    writeHelp: "Leave it empty to keep the words spelled as you typed them. Several lines are fine. {date} and {time} become today's date and the time.",
    replacementPlaceholder: "Sent on {date} at {time}",
    everyProfile: "Every profile",
    everyApp: "Every app",
    addApp: "Add app",
    removeApp: name => "Remove " + name,
    use: "Use it",
    useHelp: "Off, Speecher keeps it here and doesn't use it.",
    keyTermTip: "Key term: sent to the speech service as a hint.",
    off: "Off",
    noEntries: "No vocabulary yet",
    noEntriesHelp: "Add a name you say often, a phrase that becomes exact text, or import a list.",
    noMatch: "Nothing matches",
    noMatchHelp: "Try another spelling or filter, or add it as a new entry.",
    noLimited: "Nothing is limited to it yet",
    noLimitedHelp: "Add an entry and limit it here, or widen the filter.",
    undoCorrection: "Undo last correction",
    learning: "Learning from your edits",
    learningHelp: "Learned corrections appear in the list, limited to the app you fixed them in.",
    aRemovedApp: "a removed app"
};

// "Imported 2 entries.", or that nothing was new.
function importedText(added) {
    return added ? "Imported " + added + (added === 1 ? " entry." : " entries.") : t.nothingNew
}
function importFailedText(error) { return "Nothing imported. " + error }
// "Learned from 4 edits, 92% sure."
function evidenceText(entry) {
    return "Learned from " + (entry.evidence === 1 ? "1 edit" : entry.evidence + " edits") + ", "
        + Math.round(entry.confidence * 100) + "% sure."
}
function usesText(uses) { return uses === 1 ? "1 use" : uses + " uses" }
// "2 entries are limited to this profile.", the line beside a link to them.
function limitedHere(count, place) {
    return count === 0 ? "No vocabulary entries are limited to this " + place + "."
         : (count === 1 ? "1 vocabulary entry is" : count + " vocabulary entries are") + " limited to this " + place + "."
}
var showThem = "Show in Vocabulary";
