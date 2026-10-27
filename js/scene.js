// The pill and the voice clips that slide into it, drawn on two fixed canvases.

const COLORS = { lilac: "#9b88ee", sage: "#6aa77d", butter: "#ebbc32", coral: "#ec7552", ink: "#1e1d1b" };
const PILL = "#1f1e1c", BAR = "#f2efe6";
const BARS = 15;

const clamp = (x, a = 0, b = 1) => Math.min(b, Math.max(a, x));
export const smooth = (a, b, x) => { const k = clamp((x - a) / (b - a)); return k * k * (3 - 2 * k); };

function rng(seed) {
  let s = seed >>> 0;
  return () => ((s = (s * 1664525 + 1013904223) >>> 0) / 4294967296);
}

// A speech-like level envelope: syllables grouped into words, with short pauses.
function envelope(n, rnd) {
  const a = new Float32Array(n);
  let i = 0;
  while (i < n) {
    const len = 3 + Math.floor(rnd() * 5);
    const peak = 0.4 + rnd() * 0.6;
    for (let j = 0; j < len && i + j < n; j++) a[i + j] = peak * Math.pow(Math.sin((Math.PI * (j + 0.5)) / len), 0.6) * (0.8 + 0.2 * rnd());
    i += len + (rnd() < 0.3 ? 1 + Math.floor(rnd() * 2) : 0);
  }
  for (let j = 0; j < n; j++) a[j] *= smooth(0, 3, j) * smooth(0, 3, n - 1 - j) * 0.85 + 0.15 * (a[j] > 0);
  return a;
}

// Upsample an envelope three times and roughen it, so it reads like a recorded voice.
function recorded(env, rnd) {
  const a = new Float32Array(env.length * 3);
  for (let i = 0; i < a.length; i++) {
    const f = i / 3, j = Math.floor(f), k = f - j;
    const v = env[j] * (1 - k) + (env[j + 1] ?? 0) * k;
    a[i] = v * (0.45 + 0.55 * rnd());
  }
  return a;
}

// Clips for the intro: each slides in from the top or an upper side and feeds into one end of the pill.
// lanes: where the copy is, so paths stay clear of it.
export function makeClips(W, H, pill, copy, phone) {
  const rnd = rng(7);
  const clips = [];
  const order = ["lilac", "sage", "butter", "coral", "ink", "lilac", "sage", "coral", "butter", "ink", "lilac", "coral"];
  const v = phone ? 200 : 520;                   // feed speed, px/s
  const unit = phone ? 0.8 : 1;
  const free = { left: 0, right: 0 };             // time each pill end is free again
  const count = phone ? 9 : 12;
  for (let k = 0; k < count; k++) {
    const side = k % 2 ? 1 : -1;                  // -1 comes from the left, 1 from the right
    const end = side < 0 ? "left" : "right";
    // "bars": chunky rounded level bars. "rec": a dense recorded waveform of thin lines.
    const kind = k % 3 === 2 ? "rec" : "bars";
    const bw = (kind === "rec" ? 2 : 4 + Math.floor(rnd() * 3)) * unit;
    const gap = (kind === "rec" ? 1.6 : 2.5 + rnd() * 1.5) * unit;
    const words = Math.round((phone ? 18 : 26) + rnd() * (phone ? 10 : 22));
    const n = kind === "rec" ? words * 3 : words;
    const step = bw + gap;
    const amp = (16 + rnd() * 18) * unit;
    const ex = pill.x + side * pill.w / 2;        // the pill end it feeds
    const T1 = (phone ? 1.25 : 1.5) + rnd() * 0.5;
    const want = 0.2 + k * (phone ? 0.5 : 0.45) + rnd() * 0.15;
    const arrive = Math.max(want + T1, free[end] + 0.12);
    const L = n * step;
    free[end] = arrive + L / v;
    // every third clip drops in from the top, beside the headline, when there is room
    const band = side < 0 ? [W * 0.03, copy.left - 70] : [copy.right + 70, W * 0.97];
    const fromTop = k % 3 === 1 && band[1] - band[0] > 100;
    let x0, y0, dx0, dy0;
    if (fromTop) {
      x0 = band[0] + (band[1] - band[0]) * (0.25 + rnd() * 0.5);
      y0 = -40 - amp;
      dx0 = 0; dy0 = 1;
    } else {
      x0 = side < 0 ? -10 : W + 10;
      y0 = phone ? pill.y - (10 + rnd() * 55) : H * (0.12 + rnd() * 0.32);
      dx0 = -side; dy0 = 0;
    }
    clips.push({
      kind, color: COLORS[order[k % order.length]], side, bw, gap, step, n, L, amp,
      data: kind === "rec" ? recorded(envelope(words, rnd), rnd) : envelope(n, rnd), start: arrive - T1, T1, v, x0, y0, dx0, dy0, ex, ey: pill.y,
      // Bezier: start, start + initial direction, end - feed direction, end. The end handle matches the feed speed.
      c1: { x: x0 + dx0 * Math.abs(ex - x0) * 0.75, y: y0 + dy0 * Math.abs(pill.y - y0) * 0.9 },
      c2: { x: ex + side * (v * T1) / (3 * 0.65), y: pill.y },
    });
  }
  return clips;
}

function bez(c, k) {
  const u = 1 - k;
  return {
    x: u * u * u * c.x0 + 3 * u * u * k * c.c1.x + 3 * u * k * k * c.c2.x + k * k * k * c.ex,
    y: u * u * u * c.y0 + 3 * u * u * k * c.c1.y + 3 * u * k * k * c.c2.y + k * k * k * c.ey,
  };
}

// Head position at time t (seconds since load), or null when not on screen.
function head(c, t) {
  const dt = t - c.start;
  if (dt < 0) return null;
  if (dt < c.T1) {
    // ease the curve parameter so a clip arrives fast and settles to the feed speed
    const k = dt / c.T1;
    return bez(c, 1 - (1 - k) * (1 - k) * 0.35 - (1 - k) * 0.65);
  }
  const fed = (dt - c.T1) * c.v;
  if (fed > c.L) return null;
  return { x: c.ex - c.side * fed, y: c.ey, fed };
}

// Draw every clip; returns the level that is entering the pill now.
export function drawClips(ctx, clips, t, pill) {
  let level = 0;
  for (const c of clips) {
    const h = head(c, t);
    if (!h) continue;
    const dir = -c.side;                          // direction of travel along x at the end
    const near = Math.abs(h.x - c.ex) + Math.abs(h.y - c.ey);
    const fit = Math.min(1, (pill.h * 0.27) / c.amp);
    const vs = fit + (1 - fit) * smooth(0, 260, near);
    const a = smooth(0, 0.25, t - c.start);
    if (h.fed !== undefined) {
      const j = Math.floor(h.fed / c.step);
      if (j < c.n) level = Math.max(level, c.data[j]);
    }
    ctx.globalAlpha = a;
    ctx.fillStyle = c.color;
    ctx.beginPath();
    const round = c.kind === "bars" ? c.bw / 2 : 0;
    const minH = c.kind === "bars" ? c.bw : 1.5;
    for (let j = 0; j < c.n; j++) {
      const x = h.x - dir * j * c.step;
      const bh = Math.max(minH, c.data[j] * c.amp * 2 * vs);
      ctx.roundRect(x - c.bw / 2, h.y - bh / 2, c.bw, bh, round);
    }
    ctx.fill();
  }
  ctx.globalAlpha = 1;
  return level;
}

export const clipsDone = (clips, t) => clips.every((c) => t - c.start > c.T1 + c.L / c.v);

// Bar heights: the newest level sits in the middle bar and spreads outwards, like the real popup's waveform.
export function createBars() {
  const hist = new Float32Array(64);
  let at = 0, last = 0;
  const bars = new Float32Array(BARS);
  return {
    push(level, t) {
      // keep one sample per 1/60 s regardless of frame rate
      const steps = Math.min(8, Math.max(1, Math.round((t - last) * 60)));
      last = t;
      for (let s = 0; s < steps; s++) { at = (at + 1) % hist.length; hist[at] = level; }
    },
    read(t) {
      for (let i = 0; i < BARS; i++) {
        const d = Math.abs(i - (BARS - 1) / 2);
        const lag = Math.round(d * 1.6);
        const v = hist[(at - lag + hist.length) % hist.length];
        const jitter = 0.82 + 0.18 * Math.sin(t * 9 + i * 1.7);
        bars[i] += (v * (1 - d * 0.045) * jitter - bars[i]) * 0.45;
      }
      return bars;
    },
  };
}

// Where the two cap discs sit, relative to the pill's centre. swap 0: disc 0 in the left cap and
// disc 1 in the right; swap 1: traded. They trade along an ellipse, one over the pill and one under.
export function capOrbit(w, h, swap) {
  const a = (w - h) / 2, b = h * 0.62, th = Math.PI * swap;
  return [{ x: -a * Math.cos(th), y: -b * Math.sin(th) }, { x: a * Math.cos(th), y: b * Math.sin(th) }];
}

// The pill: an ink capsule with 15 cream bars. caps (0 to 1) grows a white disc in each end cap,
// holding marks[0] and marks[1]; swap moves them along capOrbit.
export function drawPill(ctx, p, bars, marks) {
  if (p.a <= 0.01 || p.w < 4) return;
  const { x, y, w, h } = p;
  ctx.save();
  ctx.globalAlpha = p.a;
  ctx.translate(x, y);
  if (p.rot) ctx.rotate(p.rot);
  if (p.s && p.s !== 1) ctx.scale(p.s, p.s);
  ctx.shadowColor = "rgba(60, 45, 20, 0.28)";
  ctx.shadowBlur = h * 0.35;
  ctx.shadowOffsetY = h * 0.14;
  ctx.fillStyle = PILL;
  ctx.beginPath();
  ctx.roundRect(-w / 2, -h / 2, w, h, h / 2);
  ctx.fill();
  ctx.shadowColor = "transparent";
  // the real popup's faint outline, so the pill still reads on a dark window
  ctx.strokeStyle = "rgba(255, 255, 255, 0.16)";
  ctx.lineWidth = 1;
  ctx.stroke();

  const caps = p.caps ?? 0;
  const room = w - h * 0.8 - 2 * h * 0.8 * caps;
  const bw = Math.min(h * 0.07, room / (BARS * 2 - 1));
  const total = BARS * bw + (BARS - 1) * bw;
  const maxH = h * 0.56;
  ctx.fillStyle = BAR;
  ctx.beginPath();
  for (let i = 0; i < BARS; i++) {
    const bx = -total / 2 + i * bw * 2;
    const bh = Math.max(bw, Math.min(1, bars[i]) * maxH);
    ctx.roundRect(bx, -bh / 2, bw, bh, bw / 2);
  }
  ctx.fill();

  if (caps > 0.001) {
    // the discs grow in rather than fading, so they never show grey over the ink
    const r = h * 0.36 * smooth(0, 1, caps);
    capOrbit(w, h, p.swap ?? 0).forEach((o, i) => {
      const off = Math.abs(o.y) / (h * 0.62);          // 0 in a cap, 1 at the top or bottom of the swap
      ctx.save();
      ctx.shadowColor = `rgba(60, 45, 20, ${0.3 * off})`;
      ctx.shadowBlur = r * 0.5;
      ctx.shadowOffsetY = r * 0.15;
      ctx.fillStyle = "#fffdf8";
      ctx.beginPath(); ctx.arc(o.x, o.y, r, 0, Math.PI * 2); ctx.fill();
      ctx.shadowColor = "transparent";
      ctx.clip();
      const s = r * 1.15;
      if (marks[i].complete) ctx.drawImage(marks[i], o.x - s / 2, o.y - s / 2, s, s);
      ctx.restore();
    });
  }
  ctx.restore();
}

// A level that sounds like someone talking, for stops that dictate on their own.
const talkEnv = envelope(400, rng(3));
export const talk = (t) => {
  const f = (t * 13) % talkEnv.length;
  const i = Math.floor(f), k = f - i;
  return talkEnv[i] * (1 - k) + talkEnv[(i + 1) % talkEnv.length] * k;
};
