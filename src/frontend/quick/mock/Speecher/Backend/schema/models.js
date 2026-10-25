.pragma library

// Rows for the local models page. The library, the graphics card and the
// folder are custom: their state comes from Models.
var rows = [
    { id: "localModelBrowser", page: "models", section: "Speech models", kind: "custom",
      label: "Speech models", help: "Models run on this computer, with no account and no network once downloaded.",
      keywords: "download library whisper parakeet moonshine qwen cohere voxtral delete speed test use model offline" },

    { id: "localIdleUnload", page: "models", section: "Performance and storage", kind: "choice", control: "select",
      label: "Unload the model when idle", help: "Frees memory. The next dictation loads it again while you speak.",
      value: "10", keywords: "memory ram free unload idle timeout",
      options: [
          { id: "1", label: "After 1 minute" },
          { id: "10", label: "After 10 minutes" },
          { id: "60", label: "After 1 hour" },
          { id: "0", label: "Never" }
      ] },
    { id: "localAcceleration", page: "models", section: "Performance and storage", kind: "choice", control: "select",
      label: "Acceleration", help: "Automatic picks the fastest graphics card and falls back to the CPU.",
      runningHelp: "The loaded model is running on %1.",
      value: "auto", keywords: "gpu cuda vulkan metal rocm cpu backend hardware speed",
      options: [
          { id: "auto", label: "Automatic" },
          { id: "cpu", label: "CPU" },
          { id: "vulkan", label: "Vulkan" },
          { id: "cuda", label: "CUDA" }
      ] },
    { id: "localGraphicsCard", page: "models", section: "Performance and storage", kind: "custom",
      label: "Graphics card", help: "The card this acceleration runs on.", value: "vulkan0",
      keywords: "gpu device card nvidia amd intel" },
    { id: "localModelFolder", page: "models", section: "Performance and storage", kind: "custom",
      label: "Model folder", help: "Where downloaded models are kept.", actionLabel: "Open model folder",
      keywords: "folder directory storage disk path files location" }
];
