"use strict";
/* Throwaway settings-shell layout prototype.
   One in-memory state object shared by three structurally different shells. */

/* ---------------- state (in memory only, survives variant switches) ---------------- */

const state = {
  general: {
    activation: "toggle",
    popupLifecycle: "until-delivered",
    startStopSounds: true,
    errorSound: true,
    lastUpdateCheck: "never (updates are manual)",
  },
  audio: {
    microphone: "system-default",
    fallbackToDefault: true,
  },
  speech: {
    language: "en",
    retryOnce: true,
    subscriptionStatus: "Signed in through Claude Code — Claude Pro subscription",
  },
  cleanup: {
    provider: "anthropic",
    anthropicModel: "claude-sonnet-4-6",
    openaiModel: "gpt-5.6-luna",
    strength: "balanced",
  },
  context: {
    appIdentity: true,
    caretText: true,
    ideContext: false,
    screenshot: false,
  },
  output: {
    format: "plain",
    restoreClipboard: true,
    showReceipt: true,
    generalRule: "ctrl-v",
    terminalRule: "ctrl-shift-v",
    appRules: [{ app: "Kate", rule: "direct-edit" }],
  },
  profiles: {
    autoClassify: true,
    defaultProfile: "other",
    overrides: [{ app: "Thunderbird", profile: "email" }],
  },
  vocabulary: {
    dictionary: [{ term: "Speecher" }, { term: "KWin" }],
    replacements: [{ spoken: "cash ee OS", text: "CachyOS" }],
    snippets: [{ trigger: "sign off", text: "Best regards,\nEnzo" }],
    learningEnabled: true,
    learned: [
      { spoken: "clod", text: "Claude", app: "Kate", disabled: false, reviewed: false },
      { spoken: "way land", text: "Wayland", app: "Firefox", disabled: false, reviewed: true },
    ],
    learnedTrash: [],
  },
};

const PROFILE_OPTIONS = [
  { value: "work", label: "Work" },
  { value: "email", label: "Email" },
  { value: "personal", label: "Personal" },
  { value: "other", label: "Other" },
];

const PASTE_RULE_OPTIONS = [
  { value: "ctrl-v", label: "Paste with Ctrl+V" },
  { value: "ctrl-shift-v", label: "Paste with Ctrl+Shift+V" },
  { value: "direct-edit", label: "Direct edit (accessibility)" },
  { value: "copy-only", label: "Copy only" },
];

const get = (path) => path.split(".").reduce((o, k) => o[k], state);
const set = (path, value) => {
  const keys = path.split(".");
  const last = keys.pop();
  keys.reduce((o, k) => o[k], state)[last] = value;
};

/* ---------------- tiny DOM helpers ---------------- */

let uidN = 0;
const uid = () => `f${++uidN}`;

function el(tag, attrs = {}, ...children) {
  const node = document.createElement(tag);
  for (const [key, value] of Object.entries(attrs)) {
    if (value == null) continue;
    if (key === "class") node.className = value;
    else if (key.startsWith("on")) node[key] = value;
    else node.setAttribute(key, value);
  }
  node.append(...children.filter((c) => c != null));
  return node;
}

/* A checkbox setting rendered as a row: title (and optional description) on the
   left, the bare checkbox aligned on the right — never a label beside the box. */
function checkRow(path, title, help) {
  const input = el("input", { type: "checkbox", id: uid() });
  input.checked = get(path);
  input.onchange = () => set(path, input.checked);
  return row({ label: title, control: input, help });
}

function select(path, options, opts = {}) {
  const node = el("select", { id: uid() });
  for (const o of options) node.append(el("option", { value: o.value }, o.label));
  node.value = get(path);
  node.onchange = () => {
    set(path, node.value);
    opts.onchange?.(node.value);
  };
  return node;
}

/* A settings row: title + muted description on the left, control on the right.
   `help` is the description (A/C), also the tooltip (B) and contextual-pane text
   (C); `context` overrides the pane text when given. `wide` stacks the control
   under the title at full card width — for list editors and other roomy controls. */
function row({ label, control, help, context, wide }) {
  const node = el("div", { class: wide ? "row wide" : "row" });
  const paneText = context ?? help;
  if (paneText) node.dataset.context = paneText;
  if (help) node.title = help;

  const labelCell = el("div", { class: "row-label" });
  if (label) {
    const target = control.matches?.("input, select, button")
      ? control
      : control.querySelector?.("input, select, button");
    labelCell.append(el("label", { for: target?.id }, label));
  }
  if (help) labelCell.append(el("p", { class: "help" }, help));
  const controlCell = el("div", { class: "row-control" }, control);
  node.append(labelCell, controlCell);
  return node;
}

/* A settings group: a small muted uppercase heading above a rounded, bordered
   card whose rows are separated by subtle dividers. */
function group(title, ...rows) {
  return el("fieldset", { class: "group" },
    el("legend", {}, title),
    el("div", { class: "card" }, ...rows),
  );
}

/* Editable in-memory list. Columns: {key, label, options?} */
function listEditor({ items, columns, addLabel, empty }) {
  const rowsNode = el("div", { class: "list-rows" });
  const redraw = () => {
    rowsNode.replaceChildren();
    if (!items.length) {
      rowsNode.append(el("p", { class: "help" }, empty ?? "Nothing yet."));
    }
    items.forEach((item, index) => {
      const line = el("div", { class: "list-row" });
      columns.forEach((col, colIndex) => {
        if (colIndex === 1 && columns.length === 2 && !col.options) {
          line.append(el("span", { class: "arrow" }, "→"));
        }
        if (col.options) {
          const s = el("select", { "aria-label": col.label });
          for (const o of col.options) s.append(el("option", { value: o.value }, o.label));
          s.value = item[col.key];
          s.onchange = () => { item[col.key] = s.value; };
          line.append(s);
        } else {
          const input = el("input", {
            type: "text",
            placeholder: col.label,
            "aria-label": col.label,
          });
          input.value = item[col.key] ?? "";
          input.oninput = () => { item[col.key] = input.value; };
          line.append(input);
        }
      });
      line.append(
        el("button", {
          class: "ghost",
          type: "button",
          "aria-label": `Remove entry ${index + 1}`,
          onclick: () => { items.splice(index, 1); redraw(); },
        }, "Remove"),
      );
      rowsNode.append(line);
    });
  };
  redraw();
  const addButton = el("button", {
    type: "button",
    onclick: () => {
      items.push(Object.fromEntries(columns.map((c) => [c.key, c.options ? c.options[0].value : ""])));
      redraw();
    },
  }, addLabel);
  return el("div", { class: "list-editor" }, rowsNode, addButton);
}

/* ---------------- section content (retained product scope) ---------------- */

function buildGeneral() {
  const updateStatus = el("span", { class: "inline-status" }, `Last checked: ${state.general.lastUpdateCheck}`);
  return [
    group("Activation",
      row({
        label: "Shortcut behavior",
        control: select("general.activation", [
          { value: "toggle", label: "Toggle — press to start, press to finish" },
          { value: "push", label: "Push-to-talk — hold while speaking" },
        ]),
        help: "The global shortcut runs the speecher command. Push-to-talk uses press and release from the Global Shortcuts portal.",
      }),
    ),
    group("Popup",
      row({
        label: "Popup lifecycle",
        control: select("general.popupLifecycle", [
          { value: "while-listening", label: "Hide when listening stops" },
          { value: "until-delivered", label: "Keep visible until the receipt" },
          { value: "until-dismissed", label: "Keep visible until dismissed" },
        ]),
        help: "The popup shows the working Raw Transcript while listening, then the frozen final transcript and the delivery receipt.",
      }),
    ),
    group("Sounds",
      checkRow("general.startStopSounds", "Play start and stop sounds", "Short cues when a Dictation Session starts and finishes."),
      checkRow("general.errorSound", "Play a sound when a Dictation Session fails"),
    ),
    group("Updates",
      row({
        label: "Application updates",
        control: el("div", { class: "hstack" },
          el("button", {
            type: "button",
            onclick: () => {
              state.general.lastUpdateCheck = "just now — Speecher is up to date";
              updateStatus.textContent = `Last checked: ${state.general.lastUpdateCheck}`;
            },
          }, "Check for updates"),
          updateStatus,
        ),
        help: "Speecher never checks for updates on its own. Checking is manual.",
      }),
    ),
  ];
}

function buildAudio() {
  return [
    group("Microphone",
      row({
        label: "Input device",
        control: select("audio.microphone", [
          { value: "system-default", label: "System default" },
          { value: "builtin", label: "Built-in microphone" },
          { value: "usb-desk", label: "USB desk microphone" },
          { value: "bt-headset", label: "Headset (Bluetooth)" },
        ]),
        help: "The microphone a Dictation Session records from.",
      }),
      checkRow(
        "audio.fallbackToDefault",
        "Fall back to the system default when this device is unavailable",
        "If the selected microphone disappears, recording continues on the system default instead of failing the session.",
      ),
    ),
  ];
}

function buildSpeech() {
  const status = el("span", { class: "inline-status" }, state.speech.subscriptionStatus);
  return [
    group("Claude subscription",
      row({
        label: "Status",
        control: el("div", { class: "hstack" },
          status,
          el("button", {
            type: "button",
            onclick: () => {
              state.speech.subscriptionStatus = "Signed in through Claude Code — Claude Pro subscription (checked just now)";
              status.textContent = state.speech.subscriptionStatus;
            },
          }, "Re-check"),
        ),
        help: "Speech uses your existing Claude.ai session via Claude Code credentials. Speecher never stores its own speech credential.",
      }),
    ),
    group("Language",
      row({
        label: "Spoken language",
        control: select("speech.language", [{ value: "en", label: "English" }]),
        help: "The initial release is English-only. More languages depend on the speech provider.",
      }),
    ),
    group("Reliability",
      checkRow(
        "speech.retryOnce",
        "Retry once with the full captured audio if the speech attempt fails",
        "One automatic replay of the whole recording on a transient failure. Never more than one retry, and never after cancellation.",
      ),
    ),
  ];
}

function buildCleanup() {
  const anthropicModel = select("cleanup.anthropicModel", [
    { value: "claude-sonnet-4-6", label: "Claude Sonnet 4.6 (default)" },
    { value: "claude-opus-4-8", label: "Claude Opus 4.8" },
    { value: "claude-haiku-4-5", label: "Claude Haiku 4.5" },
  ]);
  const openaiModel = select("cleanup.openaiModel", [
    { value: "gpt-5.6-luna", label: "GPT-5.6 Luna (default)" },
    { value: "gpt-5.6-terra", label: "GPT-5.6 Terra" },
    { value: "gpt-5.6-sol", label: "GPT-5.6 Sol" },
  ]);
  const strength = select("cleanup.strength", [
    { value: "light_cleanup", label: "Light cleanup — fix punctuation and obvious mistakes" },
    { value: "balanced", label: "Balanced — natural dictation, close to what was said" },
    { value: "thorough_cleanup", label: "Thorough cleanup — fix grammar while preserving meaning" },
  ]);
  const sync = (provider) => {
    anthropicModel.disabled = provider !== "anthropic";
    openaiModel.disabled = provider !== "openai";
    strength.disabled = provider === "off";
  };
  const provider = select("cleanup.provider", [
    { value: "off", label: "Off — deliver the Raw Transcript" },
    { value: "anthropic", label: "Anthropic (Claude)" },
    { value: "openai", label: "OpenAI" },
  ], { onchange: sync });
  sync(state.cleanup.provider);

  return [
    group("Cleanup",
      row({
        label: "Cleanup provider",
        control: provider,
        help: "Optional language-model cleanup that turns the Raw Transcript into the Refined Transcript. Off delivers the Raw Transcript unchanged.",
      }),
      row({
        label: "Cleanup strength",
        control: strength,
        help: "How much the transcript may be transformed. Always-on preservation rules apply at every strength.",
      }),
    ),
    group("Models",
      row({ label: "Claude model", control: anthropicModel, help: "Used when the cleanup provider is Anthropic." }),
      row({ label: "OpenAI model", control: openaiModel, help: "Used when the cleanup provider is OpenAI." }),
    ),
  ];
}

function buildContext() {
  return [
    group("Target context",
      checkRow(
        "context.appIdentity",
        "Capture the Target application's identity",
        "The application and window captured when a Dictation Session starts. Needed for Writing Profiles and per-app Paste Rules.",
      ),
      checkRow(
        "context.caretText",
        "Read caret, selection, and nearby text at the Target",
        "A small bounded snippet around the caret via accessibility. Never whole documents, and never password fields.",
      ),
    ),
    group("Richer context",
      checkRow(
        "context.ideContext",
        "Use IDE plugin context when an IDE plugin is installed",
        "Opt-in editor plugins (VS Code, Kate, JetBrains) provide document, language, and selection context beyond what accessibility exposes.",
      ),
      checkRow(
        "context.screenshot",
        "Attach a screenshot of the Target as cleanup context",
        "Off by default and available only with an image-capable cleanup model. The screenshot is sent for that session only and is not stored.",
      ),
    ),
  ];
}

function buildOutput() {
  const receiptLegend = el("dl", { class: "legend" },
    el("div", {}, el("dt", {}, "Copied"), el("dd", {}, "The transcript is on the clipboard.")),
    el("div", {}, el("dt", {}, "Input sent"), el("dd", {}, "A paste keystroke was sent; the Target's reaction is unknown.")),
    el("div", {}, el("dt", {}, "Accepted by Target"), el("dd", {}, "The Target reported taking the edit.")),
    el("div", {}, el("dt", {}, "Verified in Target"), el("dd", {}, "Speecher read the text back and it matches.")),
  );
  return [
    group("Format",
      row({
        label: "Default format",
        control: select("output.format", [
          { value: "plain", label: "Plain text (default)" },
          { value: "html", label: "HTML — plain text always included as fallback" },
        ]),
        help: "A per-session override (speecher toggle --format=html) applies to that Dictation Session only and never changes this default.",
      }),
    ),
    group("Clipboard",
      checkRow(
        "output.restoreClipboard",
        "Restore the previous clipboard contents after paste",
        "Only after Speecher verifies insertion. If delivery falls back to Copy, the transcript stays on the clipboard. Clipboard managers may still retain it.",
      ),
    ),
    group("Delivery receipt",
      checkRow("output.showReceipt", "Show the delivery receipt in the popup"),
      row({
        label: "Receipt states",
        control: receiptLegend,
        help: "Speecher reports the strongest state it can prove and never collapses these into a single “Delivered”.",
        wide: true,
      }),
    ),
    group("Paste Rules",
      row({
        label: "General rule",
        control: select("output.generalRule", PASTE_RULE_OPTIONS),
        help: "Precedence: an application rule wins over the Terminals rule, which wins over this general rule.",
        context: "Paste Rule precedence, most specific first: a rule for the exact application, then the Terminals category, then this general rule. The safe fallback is always Copy.",
      }),
      row({
        label: "Terminals",
        control: select("output.terminalRule", PASTE_RULE_OPTIONS),
        help: "Terminals usually paste with Ctrl+Shift+V, and terminal prompts may be reading secrets — rules for them stay conservative.",
      }),
      row({
        label: "Application rules",
        control: listEditor({
          items: state.output.appRules,
          columns: [
            { key: "app", label: "Application" },
            { key: "rule", label: "Paste Rule", options: PASTE_RULE_OPTIONS },
          ],
          addLabel: "Add application rule",
          empty: "No application-specific rules.",
        }),
        help: "Per-application Paste Rules override the category and general rules above.",
        wide: true,
      }),
    ),
  ];
}

function buildProfiles() {
  return [
    group("Classification",
      checkRow(
        "profiles.autoClassify",
        "Classify each Dictation Session automatically from the Target",
        "The Target application decides whether dictation reads as Work, Email, Personal, or Other.",
      ),
      row({
        label: "Default Writing Profile",
        control: select("profiles.defaultProfile", PROFILE_OPTIONS),
        help: "Used when automatic classification is off or unsure.",
      }),
    ),
    group("Per-application override",
      row({
        label: "Overrides",
        control: listEditor({
          items: state.profiles.overrides,
          columns: [
            { key: "app", label: "Application" },
            { key: "profile", label: "Writing Profile", options: PROFILE_OPTIONS },
          ],
          addLabel: "Add override",
          empty: "No per-application overrides.",
        }),
        help: "Force a specific Writing Profile for an application, regardless of classification.",
        wide: true,
      }),
    ),
  ];
}

function learnedCorrectionsEditor() {
  const v = state.vocabulary;
  const wrap = el("div", { class: "list-editor" });
  const rowsNode = el("div", { class: "list-rows" });
  const undoButton = el("button", { class: "ghost", type: "button" }, "Undo delete");

  const redraw = () => {
    rowsNode.replaceChildren();
    if (!v.learned.length) {
      rowsNode.append(el("p", { class: "help" }, "No Learned Corrections yet. They appear after you correct inserted text."));
    }
    v.learned.forEach((entry, index) => {
      const spoken = el("input", { type: "text", "aria-label": "Heard as" });
      spoken.value = entry.spoken;
      spoken.oninput = () => { entry.spoken = spoken.value; };
      const text = el("input", { type: "text", "aria-label": "Corrected to" });
      text.value = entry.text;
      text.oninput = () => { entry.text = text.value; };

      const line = el("div", { class: "list-row" },
        spoken, el("span", { class: "arrow" }, "→"), text,
        el("span", { class: "app-tag" }, entry.app),
      );
      if (!entry.reviewed) {
        line.append(
          el("span", { class: "badge new" }, "New"),
          el("button", { class: "ghost", type: "button", onclick: () => { entry.reviewed = true; redraw(); } }, "Keep"),
        );
      }
      if (entry.disabled) line.append(el("span", { class: "badge off" }, "Disabled"));
      line.append(
        el("button", { class: "ghost", type: "button", onclick: () => { entry.disabled = !entry.disabled; redraw(); } },
          entry.disabled ? "Enable" : "Disable"),
        el("button", { class: "ghost", type: "button", onclick: () => { v.learnedTrash.push(...v.learned.splice(index, 1)); redraw(); } },
          "Delete"),
      );
      rowsNode.append(line);
    });
    undoButton.disabled = !v.learnedTrash.length;
  };
  undoButton.onclick = () => {
    const entry = v.learnedTrash.pop();
    if (entry) v.learned.push(entry);
    redraw();
  };
  redraw();
  wrap.append(rowsNode, undoButton);
  return wrap;
}

function buildVocabulary() {
  return [
    group("Dictionary",
      row({
        label: "Vocabulary Entries",
        control: listEditor({
          items: state.vocabulary.dictionary,
          columns: [{ key: "term", label: "Term" }],
          addLabel: "Add term",
        }),
        help: "Words and phrases the speech provider should recognize and preserve as written here.",
        wide: true,
      }),
    ),
    group("Replacements",
      row({
        label: "Replacements",
        control: listEditor({
          items: state.vocabulary.replacements,
          columns: [{ key: "spoken", label: "Heard as" }, { key: "text", label: "Replace with" }],
          addLabel: "Add replacement",
        }),
        help: "Applied to the transcript after capture: whenever the left side is heard, the right side is inserted.",
        wide: true,
      }),
    ),
    group("Snippets",
      row({
        label: "Snippets",
        control: listEditor({
          items: state.vocabulary.snippets,
          columns: [{ key: "trigger", label: "Spoken trigger" }, { key: "text", label: "Expands to" }],
          addLabel: "Add Snippet",
        }),
        help: "A short spoken trigger that expands to longer text you wrote.",
        wide: true,
      }),
    ),
    group("Learned Corrections",
      checkRow(
        "vocabulary.learningEnabled",
        "Learn corrections from my edits after insertion",
        "When a high-confidence edit matches recent dictation, Speecher automatically adds a local Learned Correction. Everything stays on this machine.",
      ),
      row({
        label: "Learned entries",
        control: learnedCorrectionsEditor(),
        help: "New entries work immediately and are marked for convenient review. You can keep, edit, disable, or delete them; deletion can be undone.",
        wide: true,
      }),
    ),
  ];
}

/* ---------------- sections and variants ---------------- */

const SECTIONS = [
  { id: "general", title: "General", blurb: "Shortcut, popup, sounds, updates", intro: "How a Dictation Session starts, how the popup behaves, and manual updates.", build: buildGeneral },
  { id: "audio", title: "Audio", blurb: "Microphone and fallback", intro: "Which microphone Speecher records from, and what happens when it goes away.", build: buildAudio },
  { id: "speech", title: "Speech", blurb: "Claude subscription, language, retry", intro: "Speech-to-text runs through your existing Claude subscription. English-only for now, with one automatic retry.", build: buildSpeech },
  { id: "cleanup", title: "Cleanup", blurb: "Provider, model, strength", intro: "Optional cleanup turns the Raw Transcript into the Refined Transcript. Choose who does it and how strongly.", build: buildCleanup },
  { id: "context", title: "Context", blurb: "Target identity, caret text, IDE", intro: "What Speecher may read around the Target to classify and clean up dictation. Screenshot context is separately opt-in and off by default.", build: buildContext },
  { id: "output", title: "Output", blurb: "Format, receipts, Paste Rules", intro: "How the final text reaches the Target: format, clipboard behavior, delivery receipts, and Paste Rules.", build: buildOutput },
  { id: "profiles", title: "Writing Profiles", blurb: "Work, Email, Personal, Other", intro: "Dictation is classified as Work, Email, Personal, or Other from the Target, with a default and per-app overrides.", build: buildProfiles },
  { id: "vocabulary", title: "Vocabulary", blurb: "Terms, replacements, Snippets", intro: "Vocabulary Entries, replacements, Snippets, and locally Learned Corrections.", build: buildVocabulary },
];

const C_NAV_GROUPS = [
  { title: "Capture", ids: ["general", "audio", "speech"] },
  { title: "Understanding", ids: ["context", "profiles"] },
  { title: "Text", ids: ["cleanup", "vocabulary"] },
  { title: "Delivery", ids: ["output"] },
];

const VARIANTS = [
  { key: "A", name: "System Settings shell", render: renderA },
  { key: "B", name: "Compact expert dialog", render: renderB },
  { key: "C", name: "Control-center master/detail", render: renderC },
];

let currentVariant = "A";
let currentSection = "general";

const sectionById = (id) => SECTIONS.find((s) => s.id === id);

/* ---------------- variant A: classic System Settings / KConfigDialog ---------------- */

function renderA(root) {
  const nav = el("ul", {});
  for (const section of SECTIONS) {
    nav.append(el("li", {}, el("button", {
      type: "button",
      "aria-current": section.id === currentSection ? "true" : null,
      onclick: () => { currentSection = section.id; renderVariant(); },
    }, section.title)));
  }

  const section = sectionById(currentSection);
  const applyStatus = el("span", { class: "inline-status" }, "");
  const body = el("div", { class: "a-page-body" },
    el("h1", {}, section.title),
    ...section.build(),
  );
  const footer = el("div", { class: "a-footer" },
    el("button", { type: "button", onclick: () => { applyStatus.textContent = "Prototype: no persistence — defaults not implemented."; } }, "Defaults"),
    el("span", { class: "spacer" }),
    applyStatus,
    el("button", { type: "button", onclick: () => { applyStatus.textContent = "Applied (prototype: state is in memory only)."; } }, "Apply"),
  );

  root.append(el("div", { class: "shell-a" },
    el("nav", { class: "a-sidebar", "aria-label": "Settings categories" }, nav),
    el("div", { class: "a-page" }, body, footer),
  ));
}

/* ---------------- variant B: compact expert dialog ---------------- */

function renderB(root) {
  const filter = el("input", { type: "search", placeholder: "Filter settings…", "aria-label": "Filter settings" });
  const anchors = el("nav", { class: "b-anchors", "aria-label": "Jump to section" });
  const body = el("div", { class: "b-body" });

  for (const section of SECTIONS) {
    anchors.append(el("a", { href: `#b-${section.id}` }, section.title));
    body.append(el("section", { class: "b-section", id: `b-${section.id}` },
      el("h2", {}, section.title),
      ...section.build(),
    ));
  }

  filter.oninput = () => {
    const query = filter.value.trim().toLowerCase();
    for (const r of body.querySelectorAll(".row")) {
      r.hidden = !!query && !r.textContent.toLowerCase().includes(query);
    }
    for (const g of body.querySelectorAll("fieldset.group")) {
      g.hidden = !!query && ![...g.querySelectorAll(".row")].some((r) => !r.hidden);
    }
    for (const s of body.querySelectorAll(".b-section")) {
      s.hidden = !!query && ![...s.querySelectorAll(".row")].some((r) => !r.hidden);
    }
  };

  root.append(el("div", { class: "shell-b" },
    el("header", { class: "b-header" },
      el("div", { class: "b-title-row" }, el("h1", {}, "Speecher Preferences"), filter),
      anchors,
    ),
    body,
    el("footer", { class: "b-footer" },
      el("span", { class: "inline-status" }, "Changes apply immediately. Hover a row for help."),
      el("span", { class: "spacer" }),
      el("button", { type: "button" }, "Close"),
    ),
  ));
}

/* ---------------- variant C: control-center master/detail with context pane ---------------- */

function renderC(root) {
  const master = el("nav", { class: "c-master", "aria-label": "Settings categories" });
  for (const navGroup of C_NAV_GROUPS) {
    master.append(el("h2", {}, navGroup.title));
    const list = el("ul", {});
    for (const id of navGroup.ids) {
      const section = sectionById(id);
      list.append(el("li", {}, el("button", {
        type: "button",
        "aria-current": section.id === currentSection ? "true" : null,
        onclick: () => { currentSection = section.id; renderVariant(); },
      },
        el("span", { class: "nav-title" }, section.title),
        el("span", { class: "nav-blurb" }, section.blurb),
      )));
    }
    master.append(list);
  }

  const section = sectionById(currentSection);
  const detail = el("div", { class: "c-detail" },
    el("h1", {}, section.title),
    el("p", { class: "c-intro" }, section.intro),
    ...section.build(),
  );

  const asideBody = el("p", {}, section.intro);
  detail.addEventListener("focusin", (event) => {
    const contextRow = event.target.closest("[data-context]");
    if (contextRow) asideBody.textContent = contextRow.dataset.context;
  });

  root.append(el("div", { class: "shell-c" },
    master,
    detail,
    el("aside", { class: "c-aside", "aria-label": "About this setting" },
      el("h2", {}, "About this setting"),
      asideBody,
      el("p", { class: "c-aside-hint" }, "Focus a control to see what it does and why it matters."),
    ),
  ));
}

/* ---------------- shell: routing, switcher, keyboard ---------------- */

const appRoot = document.getElementById("app");
const switchLabel = document.getElementById("switch-label");

function renderVariant() {
  const variant = VARIANTS.find((v) => v.key === currentVariant);
  appRoot.replaceChildren();
  appRoot.className = `v-${variant.key.toLowerCase()}`;
  variant.render(appRoot);
  switchLabel.textContent = `${variant.key} · ${variant.name}`;
  document.title = `Speecher settings prototype — variant ${variant.key}`;
}

function switchBy(step) {
  const index = VARIANTS.findIndex((v) => v.key === currentVariant);
  currentVariant = VARIANTS[(index + step + VARIANTS.length) % VARIANTS.length].key;
  const url = new URL(location);
  url.searchParams.set("variant", currentVariant);
  history.pushState({}, "", url);
  renderVariant();
}

function readVariantFromUrl() {
  const requested = new URLSearchParams(location.search).get("variant")?.toUpperCase();
  currentVariant = VARIANTS.some((v) => v.key === requested) ? requested : "A";
}

document.getElementById("switch-prev").onclick = () => switchBy(-1);
document.getElementById("switch-next").onclick = () => switchBy(1);

document.addEventListener("keydown", (event) => {
  if (event.key !== "ArrowLeft" && event.key !== "ArrowRight") return;
  const target = event.target;
  if (target instanceof HTMLElement
      && (target.isContentEditable || target.matches("input, textarea, select, button"))) {
    return;
  }
  event.preventDefault();
  switchBy(event.key === "ArrowLeft" ? -1 : 1);
});

window.addEventListener("popstate", () => {
  readVariantFromUrl();
  renderVariant();
});

readVariantFromUrl();
renderVariant();
