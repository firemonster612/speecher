.pragma library

var rows = [
    { id: "openAiAuthMode", page: "accounts", section: "ChatGPT", kind: "choice", control: "select",
      label: "Sign-in",
      help: "API keys only cover cleanup; dictation needs a ChatGPT or CLI Proxy API sign-in.",
      autoFound: "Automatic is using: %1.",
      // A search for the API key lands here while another sign-in hides it.
      keyHint: "To enter an API key, choose API key saved in Speecher.",
      keywords: "login chatgpt codex api key environment source",
      options: [
          { id: "auto", label: "Automatic", help: "The first one found: the Codex app, then an API key from the environment or Speecher." },
          { id: "codex_oauth", label: "ChatGPT sign-in from the Codex app" },
          { id: "codex_api_key", label: "API key from the Codex app" },
          { id: "env", label: "API key from the environment", help: "OPENAI_API_KEY" },
          { id: "settings", label: "API key saved in Speecher" },
          { id: "cliproxy", label: "CLI Proxy API account" }
      ] },
    { id: "openAiApiKey", page: "accounts", section: "ChatGPT", kind: "secret",
      label: "API key", help: "Stored in the system keychain when there is one.",
      placeholder: "sk-…", keywords: "openai key secret token" },
    { id: "openAiCliproxyAccount", page: "accounts", section: "ChatGPT", kind: "custom",
      label: "Account", help: "The CLI Proxy API account to use.",
      keywords: "codex cli proxy account" },

    { id: "anthropicAuthMode", page: "accounts", section: "Claude", kind: "choice", control: "select",
      label: "Sign-in",
      help: "Claude Code uses whichever account it is signed in to, in its desktop app or with /login in the claude CLI.",
      keywords: "login claude code anthropic source",
      options: [
          { id: "oauth", label: "Claude Code" },
          { id: "cliproxy", label: "CLI Proxy API" }
      ] },
    { id: "anthropicCliproxyAccount", page: "accounts", section: "Claude", kind: "custom",
      label: "Account", help: "The CLI Proxy API account to use.",
      keywords: "claude cli proxy account" },

    { id: "cliproxyOauthDir", page: "accounts", section: "CLI Proxy API", kind: "text",
      label: "Account directory", help: "Where CLI Proxy API keeps its saved account files.",
      placeholder: "Detected automatically",
      keywords: "oauth folder path files auth dir" },
    { id: "cliproxyBaseUrl", page: "accounts", section: "CLI Proxy API", kind: "text",
      label: "Server URL", help: "Cleanup that signs in through CLI Proxy API goes through this server, and so does a Custom Endpoint set to it. Leave it empty to use the account files on this computer. Dictation always needs those files.",
      placeholder: "https://…",
      // The row that holds the server's settings until a sign-in uses CLI Proxy API.
      foldedLabel: "Server",
      foldedHelp: "Only needed when CLI Proxy API runs on another computer.",
      keywords: "proxy endpoint host address base url" },
    { id: "cliproxyApiKey", page: "accounts", section: "CLI Proxy API", kind: "secret",
      label: "Server API key", help: "A key the server accepts. Needed when a server URL is set.",
      placeholder: "A key the server accepts",
      keywords: "proxy secret token password" }
];
