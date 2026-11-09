// The ridge range: Speecher's voice as a stack of lit lines receding into the
// dark. The front line is now; every line behind it is the same wave a moment
// earlier, sliding back and fading.
//
// One shader draws every size on the page. It is the hero's ridges.js and the
// pill's strip.js (P2, "reflected range") in one: a look picks the view, and
// lerpLook() moves between them, which is how the hero folds into the pill.
//   hero: perspective (persp, narrow, horizon), no fold
//   P2:   linear rise, the stack folded about the strip's centre line
// From studio/wave/c (ridges.js, strip.js); the maths is theirs, unchanged.
(() => {
const HIST = 256;   // history samples, newest first
const RATE = 60;    // samples per second

// The voice, made pleasant: auto level against the speaker's recent peak and
// the room's floor, square-root loudness, a spring on the amplitude.
class Voice {
  constructor() { this.hist = new Float32Array(HIST); this.reduced = false; this.reset(); }
  reset() {
    this.hist.fill(0); this.count = 0; this.acc = 0;
    this.amp = 0; this.vel = 0; this.peak = .2; this.floor = 0;
  }
  update(dt, loudness) {
    this.acc += dt / 1000 * RATE;
    while (this.acc >= 1) { this.acc -= 1; this.tick(loudness); }
  }
  tick(L) {
    const h = 1 / RATE;
    this.floor = L < this.floor ? L : this.floor + (L - this.floor) * h / 4;
    this.peak = Math.max(L, this.peak * Math.exp(-h / 3), this.floor + .15);
    const target = Math.sqrt(Math.min(1, Math.max(0, (L - this.floor) / (this.peak - this.floor))));
    if (this.reduced) {
      this.amp += (target * .6 - this.amp) * (1 - Math.exp(-h / .9));
      this.vel = 0;
      this.hist.fill(this.amp);
    } else {
      const k = 240, c = 2 * Math.sqrt(k) * .6;
      this.vel += (k * (target - this.amp) - c * this.vel) * h;
      this.amp += this.vel * h;
      this.hist.copyWithin(1, 0);
      this.hist[0] = Math.max(0, this.amp);
    }
    this.count++;
  }
  get clock() { return this.count + this.acc; }
}

// Someone talking, as loudness. Syllables come from a script (onsets in ms and
// peaks) or, when babbling, from a seeded rhythm of words and short pauses, so
// captures repeat exactly. Attack and decay as in plan/envelope.js.
const PHRASE = [
  [0, .55], [140, .5], [300, .7], [470, .6], [600, .45], [760, .8], [1000, .35],
  [1500, .4],
  [2600, .5], [2760, .7], [2940, .6], [3160, .65], [3300, .5], [3480, .75], [3660, .55], [3820, .6],
  [4200, .6], [4360, .5], [4540, .7], [4700, .55], [4900, .8],
];
class Talker {
  constructor(seed = 7) {
    this.seed = seed; this.loudness = 0; this.t = 0; this.peak = 0; this.last = -1e9;
    this.queue = []; this.mode = 'quiet'; this.next = 0; this.left = 0; this.loopGap = 1400;
  }
  random() { this.seed = (this.seed * 16807) % 2147483647; return this.seed / 2147483647; }
  // 'quiet', 'babble' (talking on and on) or 'phrase' (the test phrase, looped).
  set(mode) {
    if (mode === this.mode) return;
    this.mode = mode; this.queue = []; this.next = this.t + 60;
    if (mode === 'phrase') this.queuePhrase(this.t + 300);
  }
  queuePhrase(at) { this.queue = PHRASE.map(([ms, p]) => [at + ms, p]); }
  update(dt) {
    this.t += dt;
    if (this.mode === 'phrase') {
      while (this.queue.length && this.queue[0][0] <= this.t) this.hit(this.queue.shift()[1]);
      if (!this.queue.length && this.t - this.last > this.loopGap) this.queuePhrase(this.t + 100);
    } else if (this.mode === 'babble' && this.t >= this.next) {
      this.hit(.42 + this.random() * .4);
      if (--this.left > 0) this.next = this.t + 120 + this.random() * 110;
      else { this.left = 3 + Math.floor(this.random() * 7); this.next = this.t + 260 + this.random() * 420; }
    }
    const target = Math.max(0, this.peak * Math.exp(-(this.t - this.last) / 220));
    const k = target > this.loudness ? 40 : 220;
    this.loudness += (target - this.loudness) * (1 - Math.exp(-dt / k));
  }
  hit(peak) { this.peak = peak; this.last = this.t; }
}

const VERT = `#version 300 es
in vec2 a;
uniform vec4 uQuad;   // the area to shade, in clip space: x0, y0, x1, y1
void main() { gl_Position = vec4(mix(uQuad.xy, uQuad.zw, a), 0., 1.); }`;

const FRAG = `#version 300 es
precision highp float;
uniform sampler2D uHist;
uniform vec4 uBox;          // the band in device px: left, bottom, width, height
uniform float uLines, uRow, uFrac, uSub, uSpread, uTime, uRest, uSwell;
uniform float uAmp, uBase, uHorizon, uPersp, uNarrow, uRise, uAmpFall;
uniform float uAxis;        // 0: the range stands on the band's bottom; .5: on its centre line
uniform float uFold;        // how much of the range is mirrored below that line
uniform float uFadePow, uEdge, uSwLit, uSwHalo;
uniform float uWork, uSweep, uClear, uAlpha;
uniform float uWidth, uGlow, uGain;
uniform vec3 uCore, uFar, uHalo;
// The hover warp: the cursor in band px (from the box's left and the axis),
// its strength (0..1, sprung in JS) and radius. Lines near it bend away,
// like a soft lens drawn through the light.
uniform vec3 uWarp;   // x, y (above the axis), strength
uniform float uWarpR;
out vec4 o;

float level(float age) { return texture(uHist, vec2((max(age, 0.) + .5) / ${HIST}., .5)).r; }

// Light and coverage of the stack at height y above the axis.
vec4 stack(float y, float u, float S, float px, float my) {
  vec3 col = vec3(0.);
  float cov = 0.;
  float occ = -1e4;
  float edge = mix(1., smoothstep(1., .86, abs(u)), uEdge);
  for (int i = 0; i < 48; i++) {
    if (float(i) > uLines) break;
    float d = i == 0 ? 0. : float(i) - 1. + uFrac;
    float dd = d + uClear * uClear * uLines * 1.5;
    float z = 1. + dd * uPersp;
    float x = u * (1. + dd * uNarrow);
    float r = sqrt(x * x + .0016);
    float env = level(d * uRow + r * uSpread - uSub);
    float t = uTime - d * uRow / ${RATE}.;
    float wave = .5 + .35 * cos(r * 21. - t * 7.) + .15 * cos(r * 37. - t * 11. + 1.3);
    float swell = uWork * pow(.5 + .5 * cos(6.2832 * (d / uSwell + uSweep)), 6.);
    float bell = exp(-r * r * 2.2) * smoothstep(1., .6, r);
    float h = (uRest + env + swell * .55) * bell * (.4 + .6 * wave);
    float base = uBase + (uHorizon - uBase) * (1. - 1. / z) + uRise * dd / max(uLines, 1.);
    float c = (base + uAmp * h / (1. + dd * uAmpFall)) * S;
    if (uWarp.z > .001) {
      vec2 dv = vec2(px - uWarp.x, c - my);
      float g = exp(-dot(dv, dv) / (uWarpR * uWarpR));
      c = max(c + uWarp.z * uWarpR * .4 * g * dv.y / (abs(dv.y) + uWarpR * .3), base * S);
    }
    float s = dFdx(c);
    float slope = sqrt(1. + s * s);
    float dist = abs(y - c) / slope;
    float depth = clamp(dd / (uLines + 1.), 0., 1.);
    float fade = pow(1. - depth, uFadePow) * smoothstep(1., .82, r) * (1. - uClear) * (i == 0 ? 1. : smoothstep(0., 1., d));
    float lit = fade * edge * (.55 + .75 * env + swell * uSwLit) / sqrt(slope) * smoothstep(occ - 1., occ + .5, y);
    float core = exp(-dist * dist / (uWidth * uWidth));
    float halo = exp(-dist / uGlow) * (.18 + .5 * env + swell * uSwHalo);
    vec3 tint = mix(uCore, uFar, sqrt(depth));
    col += (tint * core + uHalo * halo) * lit;
    cov += (core + halo * .6) * lit;
    occ = max(occ, c);
  }
  return vec4(col, cov);
}

void main() {
  vec2 p = gl_FragCoord.xy - uBox.xy;
  float u = p.x / uBox.z * 2. - 1.;
  float S = uBox.w * (1. - uAxis);
  float y = p.y - uBox.w * uAxis;
  float my = uWarp.y;
  vec4 a = stack(y, u, S, p.x, my);
  if (y < 0. && uFold > 0.) a = mix(a, stack(-y, u, S, p.x, -my), uFold);
  float n = fract(sin(dot(gl_FragCoord.xy, vec2(12.9898, 78.233))) * 43758.5453) - .5;
  vec3 col = a.rgb * uGain + n / 255.;
  o = vec4(min(col, 1.), clamp(a.a * uGain * .8, 0., 1.)) * uAlpha;
}`;

const hex = (h) => [1, 3, 5].map((i) => parseInt(h.slice(i, i + 2), 16) / 255);
const COLOURS = { core: '#F2F6FF', far: '#3C5FD8', halo: '#5D7FF2' };

// Sizes in css px; base, horizon, rise and amp are fractions of the band.
const HERO = { lines: 26, row: 3, spread: 26, persp: .05, narrow: .022, base: .08, horizon: .9, rise: 0, ampFall: .05,
  amp: .44, rest: .05, swell: 11, width: 1.1, glow: 6, gain: 1, axis: 0, fold: 0, fadePow: 1.7, edge: 0, swLit: 1.4, swHalo: .4 };
// P2 at the pill's 28 px band; sharp screens get more, finer ridges (strip.js).
const pillLook = (density) => {
  const sharp = density >= 1.75;
  return { lines: sharp ? 6 : 4, row: sharp ? 8 : 12, spread: 26, persp: 0, narrow: 0, base: 0, horizon: 0, rise: .96, ampFall: .04,
    amp: .62, rest: .05, swell: 4, width: sharp ? .5 : .8, glow: sharp ? 1.8 : 1.4, gain: .9, axis: .5, fold: 1, fadePow: 1.5, edge: 1,
    swLit: 1.6, swHalo: .5 };
};
const lerpLook = (a, b, t) => Object.fromEntries(Object.keys(a).map((k) => [k, a[k] + (b[k] - a[k]) * t]));

const UNIFORMS = { lines: 'uLines', row: 'uRow', spread: 'uSpread', persp: 'uPersp', narrow: 'uNarrow', base: 'uBase',
  horizon: 'uHorizon', rise: 'uRise', ampFall: 'uAmpFall', amp: 'uAmp', swell: 'uSwell', gain: 'uGain', axis: 'uAxis',
  fold: 'uFold', fadePow: 'uFadePow', edge: 'uEdge', swLit: 'uSwLit', swHalo: 'uSwHalo' };

// One canvas, one band. The canvas's backing size is the owner's (fit()).
class Range {
  constructor(canvas, voice, look) {
    this.canvas = canvas; this.voice = voice; this.look = look;
    this.alpha = 1; this.box = [0, 0, 1, 1]; this.k = 1; this.scissor = null;
    // { x, y, s }: the cursor in css px from the canvas's top left, and the warp's strength.
    this.warp = null; this.warpRadius = 90;
    // The still image behind this canvas, shown until it draws and again if its context is lost.
    const prev = canvas.previousElementSibling;
    this.fallback = prev && prev.classList.contains('range-still') ? prev : null;
    // The light's colours: core, far ridges and halo (a profile or section can tint them).
    this.colours = { core: hex(COLOURS.core), far: hex(COLOURS.far), halo: hex(COLOURS.halo) };
    const gl = this.gl = canvas.getContext('webgl2', { premultipliedAlpha: true, antialias: false, alpha: true });
    if (!gl) { this.failed = true; return; }
    canvas.addEventListener('webglcontextlost', (e) => { e.preventDefault(); this.lost = true; if (this.fallback) this.fallback.hidden = false; });
    const sh = (type, src) => {
      const s = gl.createShader(type); gl.shaderSource(s, src); gl.compileShader(s);
      if (!gl.getShaderParameter(s, gl.COMPILE_STATUS)) throw new Error(gl.getShaderInfoLog(s));
      return s;
    };
    const prog = gl.createProgram();
    gl.attachShader(prog, sh(gl.VERTEX_SHADER, VERT));
    gl.attachShader(prog, sh(gl.FRAGMENT_SHADER, FRAG));
    gl.linkProgram(prog);
    gl.useProgram(prog);
    gl.bindBuffer(gl.ARRAY_BUFFER, gl.createBuffer());
    gl.bufferData(gl.ARRAY_BUFFER, new Float32Array([0, 0, 1, 0, 0, 1, 0, 1, 1, 0, 1, 1]), gl.STATIC_DRAW);
    gl.enableVertexAttribArray(0);
    gl.vertexAttribPointer(0, 2, gl.FLOAT, false, 0, 0);
    this.tex = gl.createTexture();
    gl.bindTexture(gl.TEXTURE_2D, this.tex);
    gl.texImage2D(gl.TEXTURE_2D, 0, gl.R16F, HIST, 1, 0, gl.RED, gl.FLOAT, voice.hist);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
    gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
    this.u = {};
    const n = gl.getProgramParameter(prog, gl.ACTIVE_UNIFORMS);
    for (let i = 0; i < n; i++) { const name = gl.getActiveUniform(prog, i).name; this.u[name] = gl.getUniformLocation(prog, name); }
  }
  // The canvas's backing store: its css size times density (capped).
  fit(density) {
    const w = Math.max(1, Math.round(this.canvas.clientWidth * density));
    const h = Math.max(1, Math.round(this.canvas.clientHeight * density));
    if (this.canvas.width !== w || this.canvas.height !== h) { this.canvas.width = w; this.canvas.height = h; }
    this.k = density;
  }
  // The band in css px from the canvas's top left: left, vertical centre, width, height.
  layout(x, cy, w, h) {
    const k = this.k;
    this.box = [x * k, this.canvas.height - (cy + h / 2) * k, w * k, h * k];
  }
  clear() {
    const { gl } = this;
    if (!gl) return;
    gl.viewport(0, 0, this.canvas.width, this.canvas.height);
    gl.clearColor(0, 0, 0, 0); gl.clear(gl.COLOR_BUFFER_BIT);
  }
  // state: { work 0..1, sweep (cycles), clear 0..1, breath 0..1 }
  render(state = {}) {
    const { gl, u, look, voice } = this;
    if (!gl || this.lost) return;
    this.clear();
    if (this.alpha <= 0.001) return;
    const W = this.canvas.width, H = this.canvas.height;
    // Shade only around the band: the glow reaches a little past it.
    const [bx, by, bw, bh] = this.box;
    const m = look.glow * this.k * 6 + 2;
    const top = by + bh * 1.3 + m, bottom = by - (look.fold > 0 ? bh * 1.3 : bh * .2) - m;
    gl.uniform4f(u.uQuad, Math.max(-1, (bx - m) / W * 2 - 1), Math.max(-1, bottom / H * 2 - 1),
      Math.min(1, (bx + bw + m) / W * 2 - 1), Math.min(1, top / H * 2 - 1));
    gl.bindTexture(gl.TEXTURE_2D, this.tex);
    gl.texSubImage2D(gl.TEXTURE_2D, 0, 0, 0, HIST, 1, gl.RED, gl.FLOAT, voice.hist);
    const still = voice.reduced;
    const clock = still ? 0 : voice.clock;
    gl.uniform4fv(u.uBox, this.box);
    for (const [key, name] of Object.entries(UNIFORMS)) gl.uniform1f(u[name], look[key]);
    gl.uniform1f(u.uFrac, still ? 0 : (clock % look.row) / look.row);
    gl.uniform1f(u.uSub, still ? 0 : voice.acc);
    gl.uniform1f(u.uTime, clock / RATE);
    gl.uniform1f(u.uRest, look.rest * (.75 + .25 * (state.breath ?? 1)));
    gl.uniform1f(u.uWork, state.work ?? 0);
    gl.uniform1f(u.uSweep, state.sweep ?? 0);
    gl.uniform1f(u.uClear, state.clear ?? 0);
    gl.uniform1f(u.uAlpha, this.alpha);
    gl.uniform3fv(u.uCore, this.colours.core);
    gl.uniform3fv(u.uFar, this.colours.far);
    gl.uniform3fv(u.uHalo, this.colours.halo);
    const w = this.warp;
    if (w && w.s > .001) {
      const k = this.k, axisY = this.box[1] + this.box[3] * look.axis;
      gl.uniform3f(u.uWarp, w.x * k - this.box[0], (this.canvas.height - w.y * k) - axisY, w.s);
    } else gl.uniform3f(u.uWarp, 0, 0, 0);
    gl.uniform1f(u.uWarpR, this.warpRadius * this.k);
    gl.uniform1f(u.uWidth, look.width * this.k);
    gl.uniform1f(u.uGlow, look.glow * this.k);
    // An optional clip in css px from the canvas's top left (the pill's bar).
    const c = this.scissor;
    if (c) {
      gl.enable(gl.SCISSOR_TEST);
      gl.scissor(Math.round(c.x * this.k), Math.round(H - (c.y + c.h) * this.k), Math.round(c.w * this.k), Math.round(c.h * this.k));
    }
    gl.drawArrays(gl.TRIANGLES, 0, 6);
    if (c) gl.disable(gl.SCISSOR_TEST);
    if (this.fallback && !this.fallback.hidden) this.fallback.hidden = true;
  }
}

window.Ridge = { Voice, Talker, Range, HERO, pillLook, lerpLook, hex };
})();
