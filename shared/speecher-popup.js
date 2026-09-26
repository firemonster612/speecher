// <speecher-popup>: a web port of the desktop dictation popup.
// Geometry and motion follow src/ui/TranscriberPopup.cpp, src/ui/WaveformWidget.cpp
// and src/ui/WaveformModel.h. Theme with --sp-base, --sp-text, --sp-stroke, --sp-shadow
// and the host's font.
//
//   el.listen()            standalone capsule, live waveform
//   el.preview(text)       transcript line over the compact waveform strip
//   el.freeze(true)        bars hold at 40% alpha
//   el.status(text, words) shimmering status ("Transcribing…"), optional refinement preview
//   el.receipt(text)       plain message capsule ("Input sent")
//   el.hide()
//   el.play(script)        runs a scripted dictation; emits "phase" events

const PILL_W = 126;
const PILL_H = 48;
const COMPACT_H = 28;
const SCALE = 48 / 30;
const BAR_W = 2 * SCALE;
const BAR_GAP = 2 * SCALE;
const DOT_H = 2 * SCALE;
const BAR_R = 0.5 * SCALE;
const BARS = 15;
const BARS_W = BARS * BAR_W + (BARS - 1) * BAR_GAP;
const MARGIN = { left: 24, top: 12, right: 24, bottom: 8 };
const STRIP_SPACING = 8;
const FILLET = 12;
const LOBE_PAD = 10;
const CORNER = 24;
const SHOULDER_DROP = MARGIN.top;
const MAX_PREVIEW = 440;
const KEYFRAMES = [[0, 1], [0.2, 1.2], [0.4, 1.5], [0.8, 1.1], [0.9, 1.3], [1, 1]];

function easeInOut(s) {
  const p1 = 0.42, p2 = 0.58;
  let t = s;
  for (let i = 0; i < 6; i++) {
    const u = 1 - t;
    const x = 3 * p1 * t * u * u + 3 * p2 * t * t * u + t * t * t;
    const dx = 3 * p1 * (1 - 4 * t + 3 * t * t) + 3 * p2 * (2 * t - 3 * t * t) + 3 * t * t;
    if (dx <= 0) break;
    t = Math.min(1, Math.max(0, t - (x - s) / dx));
  }
  return t * t * (3 - 2 * t);
}

function wave(phase) {
  for (let i = 0; i < KEYFRAMES.length - 1; i++) {
    const [a, from] = KEYFRAMES[i];
    const [b, to] = KEYFRAMES[i + 1];
    if (phase > b) continue;
    return from + (to - from) * easeInOut((phase - a) / (b - a));
  }
  return 1;
}

const bulge = (i) => Math.max(0, 1 - Math.abs((BARS - 1) / 2 - i) * (2 / 48));

// The concave contour: a stadium bar around the text, fillets turning down
// beside the strip, and a rounded lobe hugging the strip's ink.
function contourPath(w, h, textBottom, stripCenter, stripInk) {
  const L = 0.5, T = 0.5, R = w - 0.5, B = h - 0.5;
  const shoulder = textBottom + SHOULDER_DROP;
  const capR = (shoulder - T) / 2;
  const lobeHalf = stripInk / 2 + LOBE_PAD;
  const lobeLeft = stripCenter - lobeHalf;
  const lobeRight = stripCenter + lobeHalf;
  const lobeHeight = B - shoulder;
  const shelf = Math.min(lobeLeft - L, R - lobeRight);
  let f = Math.min(FILLET, shelf - capR);
  let r = Math.min(CORNER, lobeHalf);
  if (f + r > lobeHeight) {
    const k = lobeHeight / (f + r);
    f *= k;
    r *= k;
  }
  if (capR <= 0 || lobeHeight <= 0 || f < 4) return null;
  return [
    `M${L + capR},${T}`, `L${R - capR},${T}`,
    `A${capR},${capR} 0 0 1 ${R - capR},${shoulder}`,
    `L${lobeRight + f},${shoulder}`,
    `A${f},${f} 0 0 0 ${lobeRight},${shoulder + f}`,
    `L${lobeRight},${B - r}`,
    `A${r},${r} 0 0 1 ${lobeRight - r},${B}`,
    `L${lobeLeft + r},${B}`,
    `A${r},${r} 0 0 1 ${lobeLeft},${B - r}`,
    `L${lobeLeft},${shoulder + f}`,
    `A${f},${f} 0 0 0 ${lobeLeft - f},${shoulder}`,
    `L${L + capR},${shoulder}`,
    `A${capR},${capR} 0 0 1 ${L + capR},${T}`, 'Z',
  ].join(' ');
}

function roundedRect(w, h, radius) {
  const L = 0.5, T = 0.5, R = w - 0.5, B = h - 0.5;
  const r = Math.min(radius, (B - T) / 2);
  return `M${L + r},${T} L${R - r},${T} A${r},${r} 0 0 1 ${R},${T + r} L${R},${B - r} A${r},${r} 0 0 1 ${R - r},${B} L${L + r},${B} A${r},${r} 0 0 1 ${L},${B - r} L${L},${T + r} A${r},${r} 0 0 1 ${L + r},${T} Z`;
}

const sleep = (ms, signal) =>
  new Promise((resolve, reject) => {
    const id = setTimeout(resolve, ms);
    signal?.addEventListener('abort', () => { clearTimeout(id); reject(signal.reason); }, { once: true });
  });

const TEMPLATE = `
<style>
  :host { display: inline-block; position: relative; vertical-align: bottom;
    --sp-base: #2b2b2b; --sp-text: #fff; --sp-stroke: rgba(255,255,255,.14); --sp-shadow: none;
    font: 14px/18px system-ui, -apple-system, "Segoe UI", "Noto Sans", sans-serif; }
  :host([hidden]) { display: inline-block !important; visibility: hidden; }
  svg { position: absolute; inset: 0; overflow: visible; filter: var(--sp-shadow); }
  path { fill: var(--sp-base); stroke: var(--sp-stroke); stroke-width: 1; }
  .text, .status, .measure { position: absolute; white-space: nowrap; color: var(--sp-text); }
  .measure { visibility: hidden; left: 0; top: 0; }
  canvas { position: absolute; }
  .status { opacity: .38; }
  .status.sweep { opacity: 1; color: transparent; -webkit-background-clip: text; background-clip: text;
    background-repeat: no-repeat; }
</style>
<svg aria-hidden="true"><path/></svg>
<span class="text"></span>
<canvas></canvas>
<span class="status"></span><span class="status sweep"></span>
<span class="measure"></span>`;

class SpeecherPopup extends HTMLElement {
  #root;
  #mode = 'wave';
  #words = '';
  #message = '';
  #frozen = false;
  #speaking = false;
  #level = { sum: 0, count: 0, windowStart: 0, target: 0, smoothed: 0 };
  #phase = 0;
  #idle = 0;
  #last = 0;
  #strip = { x: 0, y: 0, w: PILL_W, h: PILL_H };
  #raf = 0;
  #visible = true;
  #run = null;

  constructor() {
    super();
    this.#root = this.attachShadow({ mode: 'open' });
    this.#root.innerHTML = TEMPLATE;
  }

  connectedCallback() {
    this.setAttribute('role', 'img');
    this.#layout();
    new IntersectionObserver(([e]) => { this.#visible = e.isIntersecting; this.#kick(); }).observe(this);
    document.fonts?.ready.then(() => this.#layout());
    this.#kick();
  }

  disconnectedCallback() {
    cancelAnimationFrame(this.#raf);
    this.#run?.abort();
  }

  get stillMotion() { return matchMedia('(prefers-reduced-motion: reduce)').matches; }

  listen() { this.#set({ mode: 'wave', words: '', frozen: false }); }
  preview(words) { this.#set({ mode: 'wave', words }); }
  freeze(on = true) { this.#set({ frozen: on }); }
  status(text, words = '') { this.#set({ mode: 'status', message: text, words, frozen: false }); }
  receipt(text) { this.#set({ mode: 'message', message: text, words: '', frozen: false }); }
  hide() { this.hidden = true; }
  speak(on) { this.#speaking = on; }

  #set(next) {
    this.hidden = false;
    if ('mode' in next) this.#mode = next.mode;
    if ('words' in next) this.#words = next.words;
    if ('message' in next) this.#message = next.message;
    if ('frozen' in next) this.#frozen = next.frozen;
    if (this.#mode !== 'wave') this.#speaking = false;
    this.#layout();
  }

  #measure(text) {
    const m = this.#root.querySelector('.measure');
    m.textContent = text;
    return { w: m.offsetWidth, h: m.offsetHeight || 18 };
  }

  // A long line overflows from the front, keeping the words just spoken.
  #fit(words) {
    let visible = words.replace(/\s+/g, ' ').trim();
    if (this.#measure(visible).w <= MAX_PREVIEW) return visible;
    const room = MAX_PREVIEW - this.#measure('… ').w;
    while (this.#measure(visible).w > room && visible.includes(' ')) visible = visible.slice(visible.indexOf(' ') + 1);
    return '… ' + visible;
  }

  #layout() {
    if (!this.isConnected) return;
    const $ = (s) => this.#root.querySelector(s);
    const text = $('.text');
    const statuses = this.#root.querySelectorAll('.status');
    const lineH = this.#measure('Ag').h;
    const hasWords = this.#words.trim() !== '';
    const showsText = this.#mode !== 'wave';
    const ink = showsText ? this.#measure(this.#message).w : BARS_W;
    const strip = {
      w: showsText ? Math.max(PILL_W, ink + 32) : PILL_W,
      h: hasWords ? (showsText ? lineH + 6 : COMPACT_H) : Math.max(PILL_H, lineH + 10),
    };

    let w, h, path;
    if (hasWords) {
      const line = this.#fit(this.#words);
      const tw = Math.min(this.#measure(line).w, MAX_PREVIEW);
      w = Math.max(tw, strip.w) + MARGIN.left + MARGIN.right;
      h = MARGIN.top + lineH + STRIP_SPACING + strip.h + MARGIN.bottom;
      text.textContent = line;
      text.style.cssText = `left:${(w - tw) / 2}px;top:${MARGIN.top}px;display:block`;
      strip.x = (w - strip.w) / 2;
      strip.y = MARGIN.top + lineH + STRIP_SPACING;
      path = contourPath(w, h, MARGIN.top + lineH, strip.x + strip.w / 2, ink) ?? roundedRect(w, h, CORNER);
    } else {
      w = strip.w;
      h = strip.h;
      strip.x = 0;
      strip.y = 0;
      text.style.display = 'none';
      path = roundedRect(w, h, h / 2);
    }
    this.#strip = strip;

    this.style.width = `${w}px`;
    this.style.height = `${h}px`;
    const svg = $('svg');
    svg.setAttribute('width', w);
    svg.setAttribute('height', h);
    svg.setAttribute('viewBox', `0 0 ${w} ${h}`);
    $('path').setAttribute('d', path);

    for (const s of statuses) {
      s.textContent = showsText ? this.#message : '';
      s.style.cssText = `display:${showsText ? 'block' : 'none'};left:${strip.x + (strip.w - ink) / 2}px;top:${strip.y + (strip.h - lineH) / 2}px`;
    }
    statuses[1].style.display = this.#mode === 'status' ? 'block' : 'none';
    if (this.#mode === 'message') statuses[0].style.opacity = 1;
    else statuses[0].style.opacity = '';

    const canvas = $('canvas');
    canvas.style.display = showsText ? 'none' : 'block';
    const dpr = devicePixelRatio || 1;
    canvas.width = Math.round(strip.w * dpr);
    canvas.height = Math.round(strip.h * dpr);
    canvas.style.cssText += `;left:${strip.x}px;top:${strip.y}px;width:${strip.w}px;height:${strip.h}px`;

    this.setAttribute('aria-label', hasWords ? this.#words : this.#message || 'Speecher is listening');
    this.#paint();
  }

  #kick() {
    cancelAnimationFrame(this.#raf);
    if (!this.#visible) return;
    this.#last = performance.now();
    const tick = (now) => {
      const dt = Math.min((now - this.#last) / 1000, 0.1);
      this.#last = now;
      if (!this.stillMotion) {
        this.#idle += 14.2 * dt;
        if (!this.#frozen) this.#phase = (this.#phase + dt) % 1;
        if (this.#mode === 'wave' && !this.#frozen) this.#advanceLevel(now);
      }
      this.#paint();
      this.#raf = requestAnimationFrame(tick);
    };
    this.#raf = requestAnimationFrame(tick);
  }

  // Synthetic speech: loud irregular windows while speaking, near silence otherwise,
  // smoothed as WaveformModel.h's LevelModel does.
  #advanceLevel(now) {
    const l = this.#level;
    const chunk = this.#speaking ? (Math.random() < 0.18 ? 0.08 : 0.3 + Math.random() * 0.6) : 0.02;
    l.sum += chunk;
    l.count++;
    if (now - l.windowStart >= 150) {
      l.target = l.sum / l.count;
      l.sum = 0;
      l.count = 0;
      l.windowStart = now;
    }
    l.smoothed = Math.floor((l.smoothed * 0.85 + l.target * 0.15) * 100) / 100;
  }

  #paint() {
    const statusSweep = this.#root.querySelectorAll('.status')[1];
    if (this.#mode === 'status') {
      const width = this.#strip.w;
      const band = width * 0.55;
      const travel = width + band * 2;
      const pos = this.stillMotion ? band : ((this.#idle * 11) % travel) - band;
      const color = getComputedStyle(this).getPropertyValue('--sp-text').trim() || '#fff';
      const offset = (this.#strip.w - this.#measure(this.#message).w) / 2;
      statusSweep.style.backgroundImage = `linear-gradient(90deg, transparent, ${color}, transparent)`;
      statusSweep.style.backgroundSize = `${band}px 100%`;
      statusSweep.style.backgroundPosition = `${pos - offset}px 0`;
      return;
    }
    if (this.#mode !== 'wave') return;
    const canvas = this.#root.querySelector('canvas');
    const ctx = canvas.getContext('2d');
    const dpr = canvas.width / this.#strip.w || 1;
    const { w, h } = this.#strip;
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    ctx.clearRect(0, 0, w, h);
    ctx.fillStyle = getComputedStyle(this).getPropertyValue('--sp-text').trim() || '#fff';
    ctx.globalAlpha = this.#frozen ? 0.4 : 1;
    const audioScale = this.stillMotion ? 2.2 : Math.max(1, 5 * this.#level.smoothed);
    const startX = (w - BARS_W) / 2;
    for (let i = 0; i < BARS; i++) {
      const p = this.#phase - i / BARS;
      const bh = DOT_H * audioScale * bulge(i) * wave(p - Math.floor(p));
      const ry = (BAR_R * bh) / DOT_H;
      ctx.beginPath();
      ctx.roundRect?.(startX + i * (BAR_W + BAR_GAP), (h - bh) / 2, BAR_W, bh, [{ x: BAR_R, y: ry }]) ??
        ctx.rect(startX + i * (BAR_W + BAR_GAP), (h - bh) / 2, BAR_W, bh);
      ctx.fill();
    }
  }

  // Script steps, each optional:
  //   { listen: ms } { say: "words", wordMs } { stop: true } { status: "Transcribing…", ms }
  //   { refine: "clean text", wordMs, ms } { receipt: "Input sent", ms } { hide: ms } { phase: name }
  // Every step emits a "phase" event with { step, words } so the page can follow along.
  async play(script, { loop = true } = {}) {
    this.#run?.abort();
    const run = new AbortController();
    this.#run = run;
    const { signal } = run;
    const emit = (step, extra = {}) =>
      this.dispatchEvent(new CustomEvent('phase', { detail: { step, ...extra } }));
    if (this.stillMotion) {
      const last = script.findLast((s) => s.refine);
      this.status('Refining…', last?.refine ?? '');
      emit('still', { words: last?.refine ?? '' });
      return;
    }
    try {
      do {
        for (const step of script) {
          if (step.phase) emit(step.phase, step);
          if (step.listen != null) {
            this.listen();
            this.speak(false);
            emit('listening');
            await sleep(step.listen, signal);
          } else if (step.say) {
            this.speak(true);
            const words = step.say.split(' ');
            let said = this.#words ? this.#words + ' ' : '';
            for (const word of words) {
              said += word;
              this.preview(said);
              emit('word', { words: said, word });
              said += ' ';
              await sleep(step.wordMs ?? 190, signal);
            }
            this.speak(false);
            await sleep(step.pause ?? 350, signal);
          } else if (step.stop) {
            this.freeze(true);
            emit('stopped', { words: this.#words });
            await sleep(step.ms ?? 300, signal);
            this.status('Transcribing…');
            emit('transcribing');
            await sleep(step.transcribingMs ?? 900, signal);
          } else if (step.refine) {
            this.status('Refining…');
            emit('refining');
            await sleep(step.leadMs ?? 500, signal);
            let clean = '';
            for (const word of step.refine.split(' ')) {
              clean += (clean ? ' ' : '') + word;
              this.status('Refining…', clean);
              emit('refined-word', { words: clean });
              await sleep(step.wordMs ?? 110, signal);
            }
            emit('refined', { words: clean });
            await sleep(step.ms ?? 900, signal);
          } else if (step.receipt) {
            this.receipt(step.receipt);
            emit('delivered', { receipt: step.receipt });
            await sleep(step.ms ?? 1600, signal);
          } else if (step.hide != null) {
            this.hide();
            emit('hidden');
            await sleep(step.hide, signal);
          }
        }
      } while (loop && !signal.aborted);
    } catch (e) {
      if (!signal.aborted) throw e;
    }
  }

  stop() { this.#run?.abort(); }
}

customElements.define('speecher-popup', SpeecherPopup);
