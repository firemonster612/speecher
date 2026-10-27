// Illustrative dictations shown on the homepage. The page labels them as illustrative.
// `dropped` lists the raw spans that refinement removes, in order of appearance.
window.SPEECHER_SAMPLES = {
  chat: {
    target: "#team",
    role: "chat message",
    raw: "um so can we, can we push the meeting to thursday, no, friday",
    dropped: ["um", "so", "can we,", "thursday,", "no,"],
    refined: "Can we push the meeting to Friday?",
  },
  terminal: {
    target: "Konsole",
    role: "shell command",
    raw: "git commit dash m um fix the overlay closing on paste",
    dropped: ["um"],
    symbols: { "dash m": "-m" },
    refined: 'git commit -m "Fix the overlay closing on paste"',
  },
  mail: {
    target: "Mail, to Sam",
    role: "work email",
    raw: "hi sam uh the invoice for february, I meant march not february, is attached, um let me know if anything looks off, cheers",
    dropped: ["uh", "february,", "I meant march not february,", "um"],
    correction: { from: "february", to: "March", phrase: "I meant march not february" },
    refined: "Hi Sam,\n\nThe invoice for March is attached. Let me know if anything looks off.\n\nCheers",
  },
  agent: {
    target: "Claude Code",
    role: "coding agent prompt",
    raw: "run kube cuddle get pods in the staging namespace and tell me which ones are crash looping",
    dropped: [],
    beforeLearning: "Run `kube cuddle get pods` in the staging namespace and tell me which ones are crash looping.",
    learned: { heard: "kube cuddle", fixed: "kubectl" },
    refined: "Run `kubectl get pods` in the staging namespace and tell me which ones are crash looping.",
  },
};
