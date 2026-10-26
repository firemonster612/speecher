.pragma library

// The app's pages in sidebar order, which follows a dictation from start to
// finish: what starts and hears it, what turns it into text, what cleans it
// up, where it goes, then the app itself. subtitle is the line of
// explanation under a page's title. A page with a parent is one of that
// page's subpages: it sits under it in the sidebar and follows it here. A
// parent must be a top-level page, as the sidebar draws one level of
// subpages.

var pages = [
    { id: "home", title: "Home", icon: "house", file: "HomePage.qml",
      keywords: "insights stats streak words history recent" },
    { id: "transcribe", title: "Transcribe files", icon: "file-audio", file: "TranscribePage.qml",
      subtitle: "Turn meetings, interviews and voice memos into text, cleaned up the way your dictation is.",
      keywords: "audio file recording meeting podcast export" },

    { id: "dictation", title: "Dictation", icon: "mic-vocal", file: "DictationPage.qml",
      subtitle: "What starts a dictation, what it records from, and what you see and hear while it runs.",
      keywords: "dictate start record popup shortcut microphone sounds" },
    { id: "shortcuts", parent: "dictation", title: "Shortcuts", icon: "keyboard", file: "ShortcutsPage.qml",
      subtitle: "Start, pause and cancel a dictation from any app, without leaving the keyboard.",
      subtitleNoShortcuts: "Start a dictation with the speecher command, and pause or cancel it with the popup's buttons.",
      keywords: "hotkey keybinding push to talk toggle hold cancel pause" },
    { id: "microphone", parent: "dictation", title: "Microphone", icon: "mic", file: "MicrophonePage.qml",
      subtitle: "The microphone Speecher records from, and how much audio each recording keeps.",
      keywords: "input device audio recording silence level test" },
    { id: "feedback", parent: "dictation", title: "Popup & sounds", icon: "message-square-text", file: "FeedbackPage.qml",
      subtitle: "What you see and hear while you dictate. The preview follows your choices.",
      keywords: "overlay preview live text sounds media pause review" },

    { id: "speech", title: "Speech to text", icon: "audio-lines", file: "SpeechPage.qml",
      subtitle: "The service that turns your voice into text, and what Speecher uses when it is unavailable.",
      keywords: "transcription service provider language fallback endpoint whisper" },
    { id: "models", parent: "speech", title: "Local models", icon: "cpu", file: "ModelsPage.qml",
      subtitle: "Speech models that run on this computer, offline once downloaded.",
      keywords: "offline download whisper gpu acceleration runner ollama lm studio" },

    { id: "refinement", title: "Refinement", icon: "spell-check", file: "RefinementPage.qml",
      subtitle: "Turns what you said into clean text before it's pasted. Your writing profile decides how much it changes.",
      keywords: "cleanup llm model ai punctuation filler prompt context screenshot" },
    { id: "profiles", parent: "refinement", title: "Writing profiles", icon: "pen-line", file: "ProfilesPage.qml",
      subtitle: "Speecher picks a profile from the app you're dictating into, then cleans up your words to match it.",
      keywords: "tone style app per-app cleanup level instructions translate" },
    { id: "vocabulary", parent: "refinement", title: "Vocabulary", icon: "book-a", file: "VocabularyPage.qml",
      subtitle: "Words Speecher spells your way, corrections it learned from your edits, and phrases that become exact text.",
      keywords: "words names spelling corrections replacements snippets terms" },

    { id: "pasting", title: "Pasting", icon: "clipboard-paste", file: "PastingPage.qml",
      subtitle: "How your words get into the app you're using.",
      keywords: "output delivery paste clipboard insertion virtual keyboard formatting" },
    { id: "appRules", parent: "pasting", title: "App rules", icon: "app-window", file: "AppRulesPage.qml",
      subtitle: "Apps that need their words pasted differently, and how Speecher tells which app you're in.",
      keywords: "per-app rules app type terminal browser recognition match exceptions" },

    { id: "general", title: "General", icon: "settings-2", file: "GeneralPage.qml",
      subtitle: "How Speecher starts, looks, signs in and updates, what it keeps, and moving it to your phone or off this computer.",
      subtitleNoStartup: "How Speecher looks, signs in and updates, what it keeps, and moving it to your phone or off this computer.",
      keywords: "login startup phone insights history uninstall remove" },
    { id: "appearance", parent: "general", title: "Appearance", icon: "palette", file: "AppearancePage.qml",
      subtitle: "How Speecher's windows look.",
      keywords: "theme dark light colors color scheme layout split framed translucent blur" },
    { id: "accounts", parent: "general", title: "Accounts", icon: "key-round", file: "AccountsPage.qml",
      subtitle: "The sign-ins Speecher uses for dictation and cleanup. Keys are kept in your system keychain.",
      keywords: "sign in login openai anthropic chatgpt claude api key cli proxy" },
    { id: "permissions", parent: "general", title: "Permissions & health", icon: "shield-check", file: "PermissionsPage.qml",
      subtitle: "What Speecher may do on this computer, and how to fix what is missing.",
      keywords: "microphone access accessibility global shortcuts virtual keyboard setup assistant checks" },
    { id: "updates", parent: "general", title: "Updates", icon: "refresh-cw", file: "UpdatesPage.qml",
      subtitle: "Which builds you get and when, and what changed in this one.",
      keywords: "version nightly stable channel release notes what's new" }
];
