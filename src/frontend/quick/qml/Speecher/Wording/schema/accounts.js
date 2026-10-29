.pragma library

var rows = [
    { id: "openAiAuthMode", page: "accounts", section: "ChatGPT", kind: "choice", control: "select",
      label: "Sign-in",
      help: "API keys only cover cleanup; dictation needs a ChatGPT or CLI Proxy API sign-in.",
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
      label: "Server URL", help: "Send text cleanup through this CLI Proxy API server.",
      placeholder: "Use the account files on this computer",
      keywords: "proxy endpoint host address base url" },
    { id: "cliproxyApiKey", page: "accounts", section: "CLI Proxy API", kind: "secret",
      label: "Server API key", help: "A key the server accepts. Needed when a server URL is set.",
      placeholder: "A key the server accepts",
      keywords: "proxy secret token password" }
];
