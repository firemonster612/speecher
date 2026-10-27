import { makeClips, drawClips, clipsDone, createBars, drawPill, talk, smooth } from "./scene.js";
import { createTypist } from "./typing.js";
import { createStops, idle } from "./stops.js";

const reduced = matchMedia("(prefers-reduced-motion: reduce)").matches;
const clipCanvas = document.querySelector(".clips");
const pillCanvas = document.querySelector(".pill");
const cctx = clipCanvas.getContext("2d");
const pctx = pillCanvas.getContext("2d");
const sec = [...document.querySelectorAll(".stop")];
const hero = sec[0];
const heroSlot = hero.querySelector(".slot");
const pieces = [...hero.querySelectorAll(".piece")];
const marks = ["assets/claude.svg", "assets/chatgpt.svg"].map((src) => Object.assign(new Image(), { src }));

// Intro timeline, in seconds
const TYPE_AT = [2.0, 2.9, 3.9, 5.0];
const END = 8.0;

const typists = pieces.map((p) => createTypist([...p.querySelectorAll(".say")]));
const stops = createStops({ reduced });
const bars = createBars();

let W = 0, H = 0, dpr = 1, phone = false;
let tops = [], holds = [];       // each stop's track: where it starts, and how long the stop holds still
let heroPill = null;              // the hero slot, with y relative to the hero's top
let clips = [];
let t0 = performance.now();
let skipped = reduced, settled = reduced, skippedAt = 0, clipsCleared = false;
let mouse = null;

const now = () => (performance.now() - t0) / 1000;

function measure() {
  W = innerWidth; H = innerHeight;
  phone = W < 700;
  dpr = Math.min(devicePixelRatio || 1, 2);
  for (const c of [clipCanvas, pillCanvas]) {
    c.width = Math.round(W * dpr); c.height = Math.round(H * dpr);
    c.style.width = W + "px"; c.style.height = H + "px";
  }
  clipsCleared = false;
  measureSlots();
  stops.measure();
  const p = heroPill;
  const copy = hero.querySelector("h1").getBoundingClientRect();
  clips = makeClips(W, H, { x: p.x, y: p.y, w: p.w, h: p.h }, { left: copy.left, right: copy.right }, phone);
}

// The hero slot moves a little as typed text grows, so this also runs twice a second.
function measureSlots() {
  const sy = scrollY;
  tops = sec.map((s) => s.parentElement.getBoundingClientRect().top + sy);
  holds = sec.map((s) => s.parentElement.offsetHeight - s.offsetHeight);
  const r = heroSlot.getBoundingClientRect();
  heroPill = { x: r.left + r.width / 2, y: r.top - hero.getBoundingClientRect().top + r.height / 2, w: r.width, h: r.height };
}

// A stop's top on screen: it scrolls in, sticks for its hold, then scrolls out.
const stopTop = (i, sy) => Math.min(Math.max(tops[i] - sy, 0), tops[i] + holds[i] - sy);

// Progress through stop i's hold, and through the screen of scroll that brings it in
const clamp01 = (x) => Math.min(1, Math.max(0, x));
const progress = (i, sy) => (holds[i] > 0 ? clamp01((sy - tops[i]) / holds[i]) : +(sy >= tops[i]));
const entry = (i, sy) => clamp01((sy - tops[i] + H) / H);

// Where a slot is now. Scenes move and turn their slots, so this reads the layout every frame:
// the centre from the box on screen, the size from the untransformed element.
function rectOf(el) {
  const r = el.getBoundingClientRect();
  return { x: r.left + r.width / 2, y: r.top + r.height / 2, w: el.offsetWidth, h: el.offsetHeight };
}

const lerp = (a, b, k) => a + (b - a) * k;
const mix = (A, B, k) => ({ x: lerp(A.x, B.x, k), y: lerp(A.y, B.y, k), w: lerp(A.w, B.w, k), h: lerp(A.h, B.h, k) });

function heroState(t) {
  const r = rectOf(heroSlot);
  let level = idle(t);
  if (mouse) {
    const near = Math.max(0, 1 - Math.hypot(mouse.x - r.x, mouse.y - r.y) / (r.w * 1.6));
    level += near * (0.1 + talk(t) * 0.35);
  }
  return { el: heroSlot, level: reduced ? 0.45 : level, a: reduced ? 1 : smooth(0, 0.45, t), s: reduced ? 1 : 0.92 + 0.08 * smooth(0, 0.45, t) };
}

// A stop's pill, in viewport coordinates
function pillOf(i, t) {
  const st = i === 0 ? heroState(t) : stops.states[i](t);
  return { ...rectOf(st.el), level: st.level, caps: st.caps ?? 0, swap: st.swap, rot: st.rot ?? 0, a: st.a ?? 1, s: st.s ?? 1 };
}

function settle() {
  if (settled) return;
  settled = true;
  hero.classList.add("done");
}

function skip() {
  if (skipped) return;
  skipped = true;
  skippedAt = now();
  pieces.forEach((p, i) => { p.classList.add("open"); typists[i].finish(i === pieces.length - 1); });
  pieces.slice(0, -1).forEach((_, i) => typists[i].hideCaret());
  settle();
}

addEventListener("pointermove", (e) => { mouse = { x: e.clientX, y: e.clientY }; }, { passive: true });
for (const ev of ["wheel", "touchstart", "keydown", "pointerdown"]) addEventListener(ev, skip, { passive: true });
addEventListener("scroll", () => { if (scrollY > 8) skip(); }, { passive: true });

// Scrolling moves stop by stop. One wheel, touch or key gesture can't carry past the next stop's
// hold, however hard the flick. When a scroll comes to rest between two stops, it finishes the move
// in the direction it was going, so a stop is never left half on screen. Links and Home/End go anywhere.
let restY = 0, restTimer = 0, gesture = false, lastY = 0, dir = 1;
// 2 px of slack: tops are fractional on some window heights, and scrollY is rounded.
const stopAt = (y) => { let i = 0; while (i < tops.length - 1 && y >= tops[i + 1] - 2) i++; return i; };
const restSoon = () => { clearTimeout(restTimer); restTimer = setTimeout(settleScroll, 140); };
for (const ev of ["wheel", "touchmove"]) addEventListener(ev, () => { gesture = true; restSoon(); }, { passive: true });
// Only scrolling keys count: Tab must be free to bring a focused link into view.
addEventListener("keydown", (e) => { gesture = ["ArrowDown", "ArrowUp", "PageDown", "PageUp", " "].includes(e.key); restSoon(); });
addEventListener("click", (e) => { if (e.target.closest('a[href^="#"]')) gesture = false; });
addEventListener("scroll", () => {
  if (scrollY !== lastY) { dir = Math.sign(scrollY - lastY); lastY = scrollY; }
  if (gesture) {
    const from = stopAt(restY);
    const lo = tops[Math.max(0, from - 1)];
    const hi = from + 1 < tops.length ? tops[from + 1] + holds[from + 1] : Infinity;
    if (scrollY < lo || scrollY > hi) scrollTo({ top: Math.min(Math.max(scrollY, lo), hi), behavior: "instant" });
  }
  restSoon();
}, { passive: true });
function settleScroll() {
  gesture = false;
  const sy = scrollY, max = document.documentElement.scrollHeight - innerHeight;
  const i = stopAt(sy), holdEnd = tops[i] + holds[i];
  if (i < tops.length - 1 && sy > holdEnd + 2 && sy < max - 2) {
    const target = Math.min(dir > 0 ? tops[i + 1] : holdEnd, max);
    scrollTo({ top: target, behavior: reduced ? "instant" : "smooth" });
    restY = target;
  } else restY = sy;
}

let lastSize = "", measuredAt = 0, pillCleared = false;
const size = () => `${innerWidth}x${innerHeight}@${devicePixelRatio}`;
function frame() {
  if (size() !== lastSize) { lastSize = size(); measure(); }
  const t = now();
  if (t - measuredAt > 0.5) { measuredAt = t; measureSlots(); }
  const sy = scrollY;
  // scrub every stop that is on screen
  for (let j = 1; j < sec.length; j++) {
    if (reduced || (sy > tops[j] - H && sy < tops[j] + holds[j] + H)) stops.update(j, progress(j, sy), entry(j, sy));
  }

  // intro: clips slide in, pieces are typed
  let feed = 0;
  const clipsLive = !reduced && (!skipped || t - skippedAt < 0.4) && !clipsDone(clips, t);
  if (clipsLive && sy < H) {
    cctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    cctx.clearRect(0, 0, W, H);
    cctx.save();
    if (skipped) cctx.globalAlpha = 1 - (t - skippedAt) / 0.4;
    cctx.translate(0, stopTop(0, sy));
    feed = drawClips(cctx, clips, t, { h: heroPill.h });
    cctx.restore();
    if (skipped) feed = 0;
    clipsCleared = false;
  } else if (!clipsCleared) {
    cctx.setTransform(1, 0, 0, 1, 0, 0);
    cctx.clearRect(0, 0, clipCanvas.width, clipCanvas.height);
    clipsCleared = true;
  }
  if (!skipped) {
    typists.forEach((ty, i) => {
      if (!ty.started && t >= TYPE_AT[i]) { pieces[i].classList.add("open"); ty.start(t); }
      ty.update(t);
      if (ty.done && i < typists.length - 1 && typists[i + 1].started) ty.hideCaret();
    });
    if (t > END) settle();
  }

  // the pill: it rests in a stop's slot through the hold, then glides to the next stop's slot
  const i = stopAt(sy);
  const k = smooth(0, 1, (sy - tops[i] - holds[i]) / ((tops[i + 1] ?? Infinity) - tops[i] - holds[i]));
  const A = pillOf(i, t);
  const B = i + 1 < sec.length ? pillOf(i + 1, t) : A;
  const P = { ...mix(A, B, k), a: lerp(A.a, B.a, k), s: lerp(A.s, B.s, k), rot: lerp(A.rot, B.rot, k), caps: lerp(A.caps, B.caps, k), swap: A.swap ?? B.swap ?? 0 };
  // vocabulary to native: the pill would cross the dictated sentence, so it ducks out of sight mid-glide
  if (i === 2) { const dip = Math.sin(Math.PI * k) ** 2; P.a *= 1 - 0.9 * dip; P.s *= 1 - 0.3 * dip; }
  let level = lerp(A.level, B.level, k);
  if (i === 0 && !skipped && feed > 0) level = Math.max(level, 0.12 + feed * 1.1);
  bars.push(level, t);
  const b = bars.read(t);
  // past the last stop the pill is gone; clear it once rather than every frame
  if (P.a > 0.01 || !pillCleared) {
    pctx.setTransform(dpr, 0, 0, dpr, 0, 0);
    pctx.clearRect(0, 0, W, H);
    drawPill(pctx, P, b, marks);
  }
  pillCleared = P.a <= 0.01;

  if (!reduced || needsFrame) requestAnimationFrame(frame);
  needsFrame = false;
}

// Reduced motion draws on demand only
let needsFrame = true;
if (reduced) {
  const again = () => { if (!needsFrame) { needsFrame = true; requestAnimationFrame(frame); } };
  addEventListener("scroll", again, { passive: true });
  addEventListener("resize", again);
  for (const m of marks) m.addEventListener("load", again);
}

await Promise.all([document.fonts.load("780 40px 'Mona Sans'"), document.fonts.load("500 16px 'Mona Sans Mono'")]);
lastSize = size();
measure();
if (reduced) {
  pieces.forEach((p, i) => { p.classList.add("open"); typists[i].finish(i === pieces.length - 1); });
  pieces.slice(0, -1).forEach((_, i) => typists[i].hideCaret());
  hero.classList.add("done");
  // a still voice shape, so the pill reads as listening
  for (let n = 0; n < 40; n++) { bars.push(0.45, n / 60); bars.read(n / 60); }
}
t0 = performance.now();
requestAnimationFrame(frame);
