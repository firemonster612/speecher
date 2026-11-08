.pragma library

// The refinement services, in order, are custom: their choices come from
// Models, and their sign-ins, runner and server live on Services.
var rows = [
    { id: "refinementEnabled", page: "refinement", section: "Clean up", kind: "toggle",
      label: "Clean up my words", help: "A service tidies what you said before it's pasted. Off, your words are pasted as spoken.",
      // What a Writing Profile says over its cleanup while this is off.
      offNote: "Refinement is off.",
      keywords: "refinement cleanup enable disable off none raw as spoken" },
    { id: "refinementServices", page: "refinement", section: "Services, in order", kind: "custom",
      label: "Services, in order",
      help: "Speecher uses the first one that works. If none does, your words are pasted as spoken.",
      addLabel: "Add a service", addHelp: "Used when the ones above are unavailable.",
      maxCount: 3,
      // Each entry's own controls: its model, and ChatGPT's and Claude's
      // thinking and speed.
      modelLabel: "Model", effortLabel: "Thinking", speedLabel: "Speed",
      effortOptions: {
          openai: [ { id: "none", label: "None" }, { id: "low", label: "Low" }, { id: "medium", label: "Medium" },
                    { id: "high", label: "High" }, { id: "xhigh", label: "Extra high" } ],
          anthropic: [ { id: "low", label: "Low" }, { id: "medium", label: "Medium" }, { id: "high", label: "High" },
                       { id: "xhigh", label: "Extra high" }, { id: "max", label: "Max" } ]
      },
      speedOptions: {
          openai: [ { id: "standard", label: "Standard" }, { id: "fast", label: "Fast" }, { id: "ultrafast", label: "Ultrafast" } ],
          anthropic: [ { id: "standard", label: "Standard" }, { id: "fast", label: "Fast" } ]
      },
      ultrafastPlans: "Ultrafast only works on ChatGPT Pro $500 and eligible Enterprise and Edu plans. Other plans run it at Standard.",
      ultrafastModels: "Ultrafast needs GPT-6 Astra or GPT-6.1 Sol. This model refines at Fast.",
      keywords: "provider service llm ai openai anthropic claude chatgpt local runner server model thinking effort reasoning speed fast ultrafast fallback backup order" },

    { id: "targetContextControl", page: "refinement", section: "What refinement can see", kind: "toggle",
      label: "Text around your cursor", help: "Lets cleanup fit what you are writing.",
      disabledHelp: "Turn on desktop accessibility to send the app's text.", usableWhen: "accessibility",
      keywords: "context surrounding text accessibility" },
    { id: "includeScreenshotContext", page: "refinement", section: "What refinement can see", kind: "toggle",
      label: "A screenshot", help: "Kept only for the current dictation. Only ChatGPT and Claude can read it.",
      disabledHelp: "Only ChatGPT and Claude refinement can use screenshots.", usableWhen: "screenshotReader",
      keywords: "screen capture image context vision screen recording permission" },
    // macOS's grant for screenshots, a System check while they are on.
    { id: "screenRecording", page: "refinement", section: "What refinement can see", kind: "custom",
      label: "Screen Recording", opens: "includeScreenshotContext" },

    { id: "customSystemPromptEnabled", page: "refinement", section: "Advanced", kind: "toggle",
      label: "Use a custom system prompt",
      help: "Replaces the built-in rules. Each profile's tone and instructions still apply.",
      keywords: "prompt rules override instructions advanced" },
    { id: "customSystemPrompt", page: "refinement", section: "Advanced", kind: "custom",
      label: "Prompt", help: "Selection editing and local models keep their own prompts.",
      keywords: "prompt edit text", enabledWhen: { id: "customSystemPromptEnabled", equals: true },
      disabledHelp: "Turn on a custom system prompt above to edit it." },
    { id: "resetCustomSystemPrompt", page: "refinement", section: "Advanced", kind: "custom",
      label: "Built-in prompt", help: "The built-in prompt, at Medium cleanup with no tone.", actionLabel: "Reset to built-in",
      keywords: "reset restore default prompt", enabledWhen: { id: "customSystemPromptEnabled", equals: true },
      disabledHelp: "The built-in prompt is in use." }
];
