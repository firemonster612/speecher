.pragma library

// Rows for the Services page and its This computer subpage: how Speecher
// signs in to ChatGPT and Claude, what This computer runs, and Your servers. Speech to text and Refinement
// put these services in order and choose each one's model; the sign-in, the
// runner and the servers live here, once. CLI Proxy API is a way to sign in
// to either service; its account is chosen in that service's card, and its
// shared rows sit in one section of their own.
var rows = [
    { id: "openAiAuthMode", page: "services", section: "ChatGPT", kind: "choice", control: "select",
      label: "Sign in with",
      help: "API keys only cover cleanup; dictation needs a ChatGPT or CLI Proxy API sign-in.",
      // What Automatic's last check found, here and under the select in place.
      autoFound: "Using %1.",
      // A search for the API key lands here while another sign-in hides it.
      keyHint: "To enter an API key, choose API key saved in Speecher.",
      keywords: "login sign in chatgpt codex openai api key environment source account",
      options: [
          { id: "auto", label: "Automatic", help: "The first one found: the Codex app, then an API key from the environment or Speecher." },
          { id: "codex_oauth", label: "ChatGPT sign-in from the Codex app" },
          { id: "codex_api_key", label: "API key from the Codex app" },
          { id: "env", label: "API key from the environment", help: "OPENAI_API_KEY" },
          { id: "settings", label: "API key saved in Speecher" },
          { id: "cliproxy", label: "CLI Proxy API account" }
      ] },
    { id: "openAiApiKey", page: "services", section: "ChatGPT", kind: "secret",
      label: "ChatGPT API key", help: "Stored in the system keychain when there is one.",
      placeholder: "sk-…", keywords: "openai key secret token" },
    { id: "openAiCliproxyAccount", page: "services", section: "ChatGPT", kind: "custom",
      label: "Account", help: "The CLI Proxy API account to use.",
      keywords: "codex cli proxy account" },

    { id: "anthropicAuthMode", page: "services", section: "Claude", kind: "choice", control: "select",
      label: "Sign in with",
      help: "Claude Code uses whichever account it is signed in to, in its desktop app or with /login in the claude CLI.",
      keywords: "login sign in claude code anthropic source account",
      options: [
          { id: "oauth", label: "Claude Code" },
          { id: "cliproxy", label: "CLI Proxy API" }
      ] },
    { id: "anthropicCliproxyAccount", page: "services", section: "Claude", kind: "custom",
      label: "Account", help: "The CLI Proxy API account to use.",
      keywords: "claude cli proxy account" },

    // CLI Proxy API's own rows, shared by ChatGPT and Claude, in one section
    // while either signs in through it.
    { id: "cliproxyOauthDir", page: "services", section: "CLI Proxy API", kind: "text",
      sectionHelp: "Shared by ChatGPT and Claude while either signs in through CLI Proxy API.",
      label: "Account directory", help: "Where CLI Proxy API keeps its saved account files.",
      placeholder: "Detected automatically",
      keywords: "cli proxy api oauth folder path files auth dir" },
    { id: "cliproxyBaseUrl", page: "services", section: "CLI Proxy API", kind: "text",
      label: "Server URL", help: "Optional. Cleanup goes through this server instead of the account files on this computer. Dictation always needs those files.",
      placeholder: "https://…",
      keywords: "cli proxy api endpoint host address base url remote" },
    { id: "cliproxyApiKey", page: "services", section: "CLI Proxy API", kind: "secret",
      label: "CLI Proxy API server key", help: "A key the server accepts. Needed when a server URL is set.",
      placeholder: "A key the server accepts",
      keywords: "cli proxy api secret token password" },

    // This computer, the Services subpage: the Local Models speech can run,
    // and the Local Runner cleanup runs on. Which model each job runs is its
    // order's choice. speechLocalModel is the downloaded models in a Fix
    // sheet.
    { id: "speechLocalModel", page: "models", section: "Speech models", kind: "custom",
      label: "Speech to text",
      // %1 is the downloaded models' names.
      help: "Downloaded: %1. Speech to text's order picks the one to use.",
      emptyHelp: "No model is downloaded yet. Download one to dictate on this computer.",
      actionLabel: "Open This computer", keywords: "local model whisper parakeet offline download this computer" },
    { id: "localRunner", page: "models", section: "Cleanup on this computer", kind: "custom",
      label: "Runner", help: "The app on this computer that runs cleanup models. Refinement's order picks the model.",
      placeholder: "Not selected",
      keywords: "runner ollama lm studio llama-server llama.cpp offline local cleanup this computer" },
    { id: "localRunnerDetect", page: "models", section: "Cleanup on this computer", kind: "custom",
      label: "Runners on this computer", help: "Checks which runners are running on this computer.",
      keywords: "look for detect scan find runners again" },

    // Your servers: one for speech, one for refinement. Each one's model is
    // its entry's in that job's order.
    { id: "speechEndpointUrl", page: "services", section: "Your server for speech to text", kind: "text",
      label: "Server URL", help: "Any server with an OpenAI-style audio transcriptions API, such as whisper.cpp.",
      placeholder: "http://localhost:8080", keywords: "your server custom endpoint url host whisper.cpp self-hosted speech" },
    { id: "speechEndpointPath", page: "services", section: "Your server for speech to text", kind: "text",
      label: "Path", help: "whisper.cpp uses /inference.",
      keywords: "server path route inference" },
    { id: "speechEndpointApiKey", page: "services", section: "Your server for speech to text", kind: "secret",
      label: "API key", help: "Optional. Stored in the system keychain when there is one.",
      placeholder: "Optional", keywords: "server token secret key bearer" },
    { id: "speechEndpointTest", page: "services", section: "Your server for speech to text", kind: "custom",
      label: "Connection", help: "Lists the server's models for Speech to text's order.", actionLabel: "Test connection", actionIcon: "plug-zap",
      keywords: "server test connection check reachable" },

    { id: "refinementEndpointUrl", page: "services", section: "Your server for refinement", kind: "text",
      label: "Server URL", help: "The API base.", placeholder: "http://localhost:11434/v1",
      // Fills in the CLI Proxy API server the sign-in names, which then
      // sends its key.
      cliproxyLabel: "Use the CLI Proxy API server", cliproxyHelp: "Fills in %1, which takes the CLI Proxy API server key.",
      keywords: "your server custom endpoint url base address host cli proxy api cleanup" },
    { id: "refinementEndpointFormat", page: "services", section: "Your server for refinement", kind: "choice", control: "select",
      label: "Format", help: "The API the server speaks.",
      keywords: "api compatible chat completions messages",
      options: [
          { id: "openai", label: "OpenAI-compatible", help: "Chat Completions" },
          { id: "anthropic", label: "Anthropic-compatible", help: "Messages" }
      ] },
    { id: "refinementEndpointApiKey", page: "services", section: "Your server for refinement", kind: "secret",
      label: "API key", help: "Optional. Stored in the system keychain when there is one.",
      placeholder: "sk-…", keywords: "token secret key server" },
    { id: "refinementEndpointTest", page: "services", section: "Your server for refinement", kind: "custom",
      label: "Connection", help: "Checks the server answers, and lists its models for Refinement's order.", actionLabel: "Test connection",
      keywords: "test check verify server" }
];
