/* Provider ratings mockups: the data every page shares, and the rating bar.
 * ?v=before|after picks the version, ?choice= the selected provider; the
 * pages build themselves from these tables. No network, no native dialogs. */

const params = new URLSearchParams(location.search);
const version = params.get('v') === 'before' ? 'before' : 'after';
const framed = params.has('framed');

/* ------------------------------------------------------- the rules */

// Every option is rated by the same two rules. Wait is the seconds between
// you stopping and the text being ready, for about 10 s of speech.
const halves = (x) => Math.round(x * 2) / 2;
const clamp = (x) => Math.min(10, Math.max(0, x));
// A point off for each percentage point of words wrong above 2%, about the
// best any service scores on Artificial Analysis.
const accuracyFor = (wer) => halves(clamp(12 - wer));
// Two points off for each second of waiting, and one more when no text shows
// while you speak.
const speedFor = (wait, live = true) => (wait == null ? null : halves(clamp(10 - 2 * wait - (live ? 0 : 1))));

/* ------------------------------------------------------- speech evidence */

// Artificial Analysis AA-WER v2 and latency, read 2026-10-05. ChatGPT Codex's
// final text comes from GPT Transcribe while "Transcribe again for accuracy"
// is on, the default; you wait for GPT Live Transcribe's last phrase (0.81 s
// on the streaming leaderboard) and then that pass (0.77 s, timed on an 11 s
// clip against chatgpt.com/backend-api/transcribe). Claude Voice is Deepgram
// Nova 3 Realtime: 6.59% streaming AA-WER, final text 0.07 s after you stop.
const CODEX_SPEECH = { wer: 3.31, wait: 0.81 + 0.77 };
const CLAUDE_SPEECH = { wer: 6.59, wait: 0.07 };

// AA-WER over FLEURS English for the three catalog models Artificial Analysis
// lists (Whisper Large v3 Turbo 4.62/4.38, Cohere Transcribe 4.57/5.08,
// Voxtral Small 2.77/3.55). Turns FLEURS into an AA-WER estimate for the rest.
const FLEURS_TO_AA = (4.62 / 4.38 + 4.57 / 5.08 + 2.77 / 3.55) / 3;

// From src/core/LocalModelCatalog.cpp. aaWer is null where Artificial Analysis
// has no figure; seconds is the catalog's estimate for 10 s of speech on this
// kind of PC (the Ryzen 4750U reference), as the setup assistant shows it.
// The wait counts all of it even for a streaming model, which overstates it.
const LOCAL_MODELS = [
  { name: 'Parakeet 0.6B', streams: true, fleurs: 3.99, aaWer: 6.43, aaSource: 'Parakeet TDT 0.6B V2, the nearest model listed', seconds: 10 / 26.01, suggested: true },
  { name: 'Moonshine Small', streams: true, fleurs: 8.55, aaWer: null, seconds: 10 / 14.16 },
  { name: 'Moonshine Medium', streams: true, fleurs: 7.87, aaWer: null, seconds: 10 / 8.9 },
  { name: 'Whisper Large v3 Turbo', streams: false, fleurs: 4.38, aaWer: 4.62, aaSource: 'Artificial Analysis', seconds: 10 / 3.69 },
  { name: 'Qwen3-ASR 1.7B', streams: false, fleurs: 3.23, aaWer: null, seconds: 10 / 3.72 },
  { name: 'Cohere Transcribe', streams: false, fleurs: 5.08, aaWer: 4.57, aaSource: 'Artificial Analysis', seconds: 10 / 8.52 },
  { name: 'Voxtral Small 24B', streams: false, fleurs: 3.55, aaWer: 2.77, aaSource: 'Artificial Analysis', seconds: null },
];
const localWer = (m) => m.aaWer ?? m.fleurs * FLEURS_TO_AA;
const localAccuracy = (m) => accuracyFor(localWer(m));
const localSpeed = (m) => speedFor(m.seconds, m.streams);
const SUGGESTED = LOCAL_MODELS.find((m) => m.suggested);

/* ------------------------------------------------------- refinement evidence */

// From bench/bench.py: 15 messy transcripts x 3 runs through
// the app's real prompts and default settings, 2026-10-05. quality is the
// share of checks passed (corrections applied, fillers gone, questions left
// unanswered, numbers and paths formatted); wait is the median total time.
// OpenAI is gpt-6-luna at effort none on the Fast tier. Anthropic is Claude
// Opus 5.5 at effort low at standard speed: its fast mode needs usage credits
// and the app falls back when a subscription has none (seen in the app log).
// The Local Runner rows ran on this CPU-only PC through Ollama 0.34.4; the app
// suggests LFM2.5 1.2B here and Gemma 4 E4B on a fast GPU, where it is quicker.
const REFINE_BENCH = {
  openai: { quality: 123 / 123, wait: 1.54 },
  anthropic: { quality: 123 / 123, wait: 2.77 },
  lfm: { name: 'LFM2.5 1.2B', quality: 66 / 123, wait: 0.57 },
  gemma: { name: 'Gemma 4 E4B', quality: 81 / 123, wait: 2.26 },
};
const refineRatings = (id) => [['Quality', halves(REFINE_BENCH[id].quality * 10)], ['Speed', speedFor(REFINE_BENCH[id].wait)]];

/* ------------------------------------------------------- providers */

const MARKS = {
  openai: '<img class="mark" src="../../assets/brand/chatgpt.svg" alt="">',
  anthropic: '<img class="mark" src="../../assets/brand/claude.svg" alt="">',
  computer:
    '<svg class="mark" viewBox="0 0 16 16" aria-hidden="true"><g fill="none" stroke="currentColor" stroke-width="1.1">' +
    '<rect x="1.5" y="2.5" width="13" height="8.5" rx=".6"/><path d="M6 13.5h4M8 11v2.5"/></g></svg>',
  server:
    '<svg class="mark" viewBox="0 0 16 16" aria-hidden="true"><g fill="currentColor">' +
    '<rect x="2" y="2" width="12" height="3.2" rx=".5"/><rect x="2" y="6.4" width="12" height="3.2" rx=".5"/>' +
    '<rect x="2" y="10.8" width="12" height="3.2" rx=".5"/></g></svg>',
};

const SPEECH = [
  {
    id: 'codex', name: 'ChatGPT Codex', mark: 'openai', status: 'Ready',
    ratings: [['Accuracy', accuracyFor(CODEX_SPEECH.wer)], ['Speed', speedFor(CODEX_SPEECH.wait)]],
    before: [['Score', '9 / 10'], ['Engine', 'GPT Live Transcribe'], ['Languages', 'Around 100'],
      ['Speed', 'A phrase at a time, after a short pause'], ['Accuracy', 'Excellent, even with accents and noise'],
      ['Formatting', 'Natural punctuation and phrasing']],
    after: [['Languages', 'Around 100'], ['Text shows', 'A phrase at a time, after a short pause'],
      ['Formatting', 'Natural punctuation and phrasing']],
    models: [['GPT Live Transcribe', 'Writes each phrase as you pause.'],
      ['GPT Transcribe', 'Goes over the whole recording when you stop, while Transcribe again for accuracy is on.']],
    settingsBefore: 'GPT Live Transcribe. Very accurate, around 100 languages.',
    settingsAfter: 'Very accurate, around 100 languages.',
  },
  {
    id: 'claude', name: 'Claude Voice', mark: 'anthropic', status: 'Not signed in',
    ratings: [['Accuracy', accuracyFor(CLAUDE_SPEECH.wer)], ['Speed', speedFor(CLAUDE_SPEECH.wait)]],
    before: [['Score', '8 / 10'], ['Engine', 'Deepgram Nova 3'], ['Languages', 'About 60'],
      ['Speed', 'Live stream; words appear as you speak'], ['Accuracy', 'Strong, holds up in noisy rooms'],
      ['Formatting', 'Automatic punctuation, capitals, numerals']],
    after: [['Languages', 'About 60'], ['Text shows', 'Live stream; words appear as you speak'],
      ['Formatting', 'Automatic punctuation, capitals, numerals']],
    models: [['Deepgram Nova 3', 'Writes your words as you speak.']],
    settingsBefore: 'Deepgram Nova 3. About 60 languages, automatic punctuation and numerals.',
    settingsAfter: 'About 60 languages, automatic punctuation and numerals.',
  },
  {
    id: 'local', name: 'Local Model', mark: 'computer', status: '',
    note: 'Runs on this computer. No account, works offline.',
    ratings: [['Accuracy', localAccuracy(SUGGESTED)], ['Speed', localSpeed(SUGGESTED)]],
    ratingsFor: `${SUGGESTED.name} on this computer`,
    models: LOCAL_MODELS.map((m) => [m.name, m.suggested ? 'Suggested for this computer' : '',
      [['Accuracy', localAccuracy(m)], ['Speed', localSpeed(m)]]]),
    settingsBefore: 'No model downloaded.',
    settingsAfter: 'No model downloaded.',
  },
];

const REFINEMENT = [
  {
    id: 'anthropic', name: 'Anthropic', mark: 'anthropic', group: 'sign-in', status: 'Not signed in',
    note: 'Uses your Claude Code sign-in.',
    ratings: refineRatings('anthropic'),
    before: [['Score', '8 / 10'], ['Default model', 'Claude Opus 5.5'], ['Speed', 'About 3 seconds per dictation (estimated)'],
      ['Efficiency', 'Always reasons, kept light at low effort'], ['Quality', 'Excellent cleanup; can leave a spoken correction in']],
    after: [['Efficiency', 'Always reasons, kept light at low effort']],
    models: [['Claude Opus 5.5', 'The default. Change it in Settings, under Refinement.']],
  },
  {
    id: 'openai', name: 'OpenAI', mark: 'openai', group: 'sign-in', status: 'Not signed in',
    note: 'Uses your ChatGPT or Codex sign-in.',
    ratings: refineRatings('openai'),
    before: [['Score', '9 / 10'], ['Default model', 'gpt-6-luna'], ['Speed', 'About 3 seconds per dictation (estimated)'],
      ['Efficiency', 'No reasoning pass; time varies run to run'], ['Quality', 'Excellent cleanup; applies spoken corrections reliably']],
    after: [['Efficiency', 'No reasoning pass; time varies run to run']],
    models: [['gpt-6-luna', 'The default. Change it in Settings, under Refinement.']],
  },
  {
    id: 'local', name: 'Local Runner', mark: 'computer', group: 'own', status: 'Ollama found',
    note: 'Runs on this computer through Ollama, LM Studio or llama-server.',
    ratings: refineRatings('lfm'),
    ratingsFor: `${REFINE_BENCH.lfm.name}, suggested for this computer`,
    models: [[REFINE_BENCH.lfm.name, 'Suggested for this computer', refineRatings('lfm')],
      [REFINE_BENCH.gemma.name, 'Suggested with a fast graphics card; timed here without one', refineRatings('gemma')]],
  },
  {
    id: 'endpoint', name: 'Custom Endpoint', mark: 'server', group: 'own', status: '',
    note: 'A server you run, or CLI Proxy API, with an OpenAI- or Anthropic-compatible API.',
  },
];

/* ------------------------------------------------------- rendering */

const format = (n) => (n == null ? '?' : `${n}/10`);

function ratingHtml([label, value]) {
  const width = value == null ? 0 : value * 10;
  return `<span class="rating${value == null ? ' unknown' : ''}" title="${label}: ${value == null ? 'not rated' : value + ' out of 10'}">` +
    `<span class="label">${label}</span><span class="bar"><span style="width:${width}%"></span></span>` +
    `<span class="value">${format(value)}</span></span>`;
}

const ratingsHtml = (ratings) => `<div class="ratings">${ratings.map(ratingHtml).join('')}</div>`;

function statsHtml(rows) {
  if (!rows || !rows.length) return '';
  return `<table class="stats"><tbody>${rows.map(([k, v]) => `<tr><th>${k}</th><td>${v}</td></tr>`).join('')}</tbody></table>`;
}

// A list of models; rated ones get Accuracy and Speed columns under a header.
function modelsHtml(models) {
  const rated = models.some(([, , ratings]) => ratings);
  const header = rated
    ? `<div class="model rated small"><span>Model</span>${models[0][2].map(([label]) => `<span>${label}</span>`).join('')}</div>` : '';
  return `<div class="models"><div class="card">${header}${models.map(([name, line, ratings]) =>
    `<div class="model${rated ? ' rated' : ''}"><span class="name">${name}${line ? `<small>${line}</small>` : ''}</span>` +
    `${ratings ? ratings.map(ratingHtml).join('') : ''}</div>`).join('')}</div></div>`;
}

// The before/after and choice links at the bottom of every page.
function viewLinks(choices, choice) {
  const here = (key, value) => {
    const next = new URLSearchParams(location.search);
    next.set(key, value);
    return `?${next}`;
  };
  const link = (key, value, label, on) => `<a href="${here(key, value)}" class="${on ? 'on' : ''}">${label}</a>`;
  return `<nav class="views">${link('v', 'before', 'Before', version === 'before')}${link('v', 'after', 'After', version === 'after')}` +
    `<span class="gap"></span>${choices.map((c) => link('choice', c, c, c === choice)).join('')}</nav>`;
}

function boot() {
  document.body.classList.add(version);
  if (framed) document.body.classList.add('framed');
  if (params.has('clean')) document.body.classList.add('clean');
  // ?open=1 opens every Advanced disclosure, for screenshots.
  if (params.get('open') === '1') {
    document.querySelectorAll('details.advanced').forEach((d) => { d.open = true; });
  }
}
