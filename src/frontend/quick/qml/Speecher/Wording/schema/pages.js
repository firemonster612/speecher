.pragma library

// The app's pages in sidebar order. Groups follow a dictation from start to
// finish: what starts it, what hears it, what cleans it up, where it goes.
// subtitle is the line of explanation under a page's title.
var groups = [
    { id: "start", title: "" },
    { id: "dictation", title: "Dictation" },
    { id: "text", title: "Text" },
    { id: "app", title: "App" }
];

var pages = [
    { id: "home", group: "start", title: "Home", icon: "house", file: "HomePage.qml",
      keywords: "insights stats streak words history recent" },
    { id: "transcribe", group: "start", title: "Transcribe files", icon: "file-audio", file: "TranscribePage.qml",
      subtitle: "Turn meetings, interviews and voice memos into text, cleaned up the way your dictation is.",
      keywords: "audio file recording meeting podcast export" },

    { id: "shortcuts", group: "dictation", title: "Shortcuts", icon: "keyboard", file: "ShortcutsPage.qml",
      subtitle: "Start, pause and cancel a dictation from any app, without leaving the keyboard.",
      keywords: "hotkey keybinding push to talk toggle hold cancel pause" },
    { id: "microphone", group: "dictation", title: "Microphone", icon: "mic", file: "MicrophonePage.qml",
      subtitle: "The microphone Speecher records from, and how much audio each recording keeps.",
      keywords: "input device audio recording silence level test" },
    { id: "speech", group: "dictation", title: "Speech to text", icon: "audio-lines", file: "SpeechPage.qml",
      subtitle: "The service that turns your voice into text, and what Speecher uses when it is unavailable.",
      keywords: "transcription service provider language fallback endpoint whisper" },
    { id: "feedback", group: "dictation", title: "Popup & sounds", icon: "message-square-text", file: "FeedbackPage.qml",
      subtitle: "What you see and hear while you dictate. The preview follows your choices.",
      keywords: "overlay preview live text sounds media pause review" },

    { id: "refinement", group: "text", title: "Refinement", icon: "spell-check", file: "RefinementPage.qml",
      subtitle: "Turns what you said into clean text before it's pasted. Your Writing Profile decides how much it changes.",
      keywords: "cleanup llm model ai punctuation filler prompt context screenshot" },
    { id: "profiles", group: "text", title: "Writing profiles", icon: "pen-line", file: "ProfilesPage.qml",
      subtitle: "Speecher picks a profile from the app you're dictating into, then cleans up your words to match it.",
      keywords: "tone style app per-app cleanup level instructions translate" },
    { id: "vocabulary", group: "text", title: "Vocabulary", icon: "book-a", file: "VocabularyPage.qml",
      subtitle: "Words Speecher spells your way, fixes it learned from your edits, and phrases that become exact text.",
      keywords: "words names spelling corrections replacements snippets terms" },
    { id: "pasting", group: "text", title: "Pasting", icon: "clipboard-paste", file: "PastingPage.qml",
      subtitle: "How your words get into the app you're using, and the apps that need it done differently.",
      keywords: "output delivery paste clipboard terminal rules apps insertion" },

    { id: "accounts", group: "app", title: "Accounts", icon: "key-round", file: "AccountsPage.qml",
      subtitle: "The sign-ins Speecher uses for dictation and cleanup. Keys are kept in your system keychain.",
      keywords: "sign in login openai anthropic chatgpt claude api key cli proxy" },
    { id: "models", group: "app", title: "Local models", icon: "cpu", file: "ModelsPage.qml",
      keywords: "offline download whisper gpu acceleration runner ollama lm studio" },
    { id: "general", group: "app", title: "General", icon: "settings-2", file: "GeneralPage.qml",
      subtitle: "How Speecher looks and starts, what it may do on this computer, and what it keeps.",
      keywords: "theme appearance login startup permissions phone insights uninstall" },
    { id: "updates", group: "app", title: "Updates", icon: "refresh-cw", file: "UpdatesPage.qml",
      subtitle: "Which builds you get and when, and what changed in this one.",
      keywords: "version nightly stable channel release notes what's new" }
];
