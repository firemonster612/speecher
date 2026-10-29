.pragma library

// Rows for the local models page. The library, the acceleration, the
// graphics card and the folder are custom: their state comes from Models.
var rows = [
    { id: "localModelBrowser", page: "models", section: "Speech models", kind: "custom",
      label: "Speech models", help: "Models run on this computer, with no account and no network once downloaded.",
      keywords: "download library whisper parakeet moonshine qwen cohere voxtral delete speed test use model offline" },

    { id: "localIdleUnload", page: "models", section: "Performance and storage", kind: "choice", control: "select",
      label: "Unload the model when idle", help: "Frees memory. The next dictation loads it again while you speak.",
      keywords: "memory ram free unload idle timeout",
      options: [
          { id: "1", label: "After 1 minute" },
          { id: "10", label: "After 10 minutes" },
          { id: "60", label: "After 1 hour" },
          { id: "0", label: "Never" }
      ] },
    { id: "localAcceleration", page: "models", section: "Performance and storage", kind: "custom",
      label: "Acceleration", help: "Automatic picks the fastest graphics card and falls back to the CPU.",
      runningHelp: "The loaded model is running on %1.",
      keywords: "gpu cuda vulkan metal rocm cpu backend hardware speed" },
    { id: "localGraphicsCard", page: "models", section: "Performance and storage", kind: "custom",
      label: "Graphics card", help: "The card this acceleration runs on.",
      keywords: "gpu device card nvidia amd intel" },
    { id: "localModelFolder", page: "models", section: "Performance and storage", kind: "custom",
      label: "Model folder", help: "Where downloaded models are kept.", actionLabel: "Open model folder",
      keywords: "folder directory storage disk path files location" }
];
