# Wispr Flow's status-bar waveform rendering

Date: 2026-09-08

Reference notes behind the Qt Widgets popup waveform port, since removed.


Source: Wispr Flow for Windows 1.6.793, extracted from `WisprFlow-1.6.793-full.nupkg` (`app.asar`, webpack bundles). The waveform lives in the `status` renderer window, the little floating pill Wispr Flow docks at the bottom (or left/right edge) of the screen.

## The bars are DOM elements, not a canvas

Each bar is a `<div>` (2px wide, 2px tall at rest, border-radius 0.5px, `transform-origin: center`). There is no canvas and no per-frame JavaScript drawing in normal dictation. The whole animation is CSS:

```css
.bar {
  width: var(--waveform-bar-width, 2px);
  min-height: var(--waveform-bar-width, 2px);
  background: hsla(0, 0%, 100%, .4);      /* 40% white when idle */
  border-radius: .5px;
  transform-origin: center;
  will-change: transform;
}
.bar.micActive { background: #fff; }      /* 100% white while capturing */
.bar.waveAnimation {
  transform: scaleY(calc(var(--audio-scale, 1) * var(--bar-height-scale, 1)));
  animation: wave 1s infinite ease-in-out;
}
@keyframes wave {
  0%   { transform: scaleY(calc(var(--audio-scale, 1) * var(--bar-height-scale, 1) * 1)); }
  20%  { ... * 1.2)); }
  40%  { ... * 1.5)); }
  80%  { ... * 1.1)); }
  90%  { ... * 1.3)); }
  100% { ... * 1)); }
}
```

So a bar's rendered height is `2px x audioScale x bulge x wave(t)`:

- `--audio-scale` (audioScale) is set per animation frame on the container from the smoothed mic level (see below). It is floored at 1, so bars never drop below the resting 2px dot.
- `--bar-height-scale` (bulge) is static per bar: `max(0, 1 - h^2 * bulge/48)` with `h = |(N-1)/2 - i|` and bulge coefficient 1 by default. Center bars ~0.99, edge bars ~0.58, which gives the row its rounded silhouette.
- `wave(t)` is the 1s keyframe loop above (1 -> 1.2 -> 1.5 -> 1.1 -> 1.3 -> 1) with CSS `ease-in-out` (cubic-bezier(0.42, 0, 0.58, 1)) easing between keyframes.

The traveling crest comes from per-bar `animation-delay: 0.1s * p[i]`, where `p[i] = i` for the first half of the bars and `i - N` for the second half (for 10 bars: 0, .1, .2, .3, .4, -.5, -.4, -.3, -.2, -.1). Modulo the 1s loop, every bar ends up exactly 0.1s behind its left neighbour, so one full cycle spans exactly the 10 bars and wraps seamlessly: a crest sweeps left to right once per second, forever, even at silence (where it pulses the 2px dots between 2 and 3px).

Default `barCount` is 10; bar width 2px, gap 2px; the waveform row is 18px tall inside the active 50x30 pill (black `--shade-black`, border `--vast-900`, radius 22.5px). Command mode turns the bars `#ffa946` and instruct mode `var(--signal-500)`.

## How the bars react to audio

The pipeline has three stages:

1. **Capture (renderer worklet + main process).** Audio is captured in ~640-sample chunks (40ms at 16kHz) by an AudioWorklet. Each chunk's volume is its RMS in dBFS: `20*log10(sqrt(mean(x^2)))` (Int16 flavor: `20*log10(max(rms,1)/32768)`).

2. **Level mapping and throttling (main process).** Each chunk's dB value goes through an adaptive noise floor:

   ```js
   scale = r => (r < floor && (floor = Math.max(-60, r)),
                 Math.max(0, Math.min(1, (r - floor) / 20)))
   ```

   The floor tracks the quietest chunk seen (clamped at -60dBFS) and maps to 0; floor + 20dB maps to 1. It persists for the app session (reset only when entering mic-test mode, which switches to a fixed `(dB + 50)/40` mapping). Every **150ms** the mean of the mapped levels since the last tick is sent over IPC (`status:audioLevel`) to the status window; ticks with no chunks send nothing, so the renderer holds its previous target.

3. **Smoothing (renderer hook).** A hook subscribes to the level events and runs a requestAnimationFrame loop while any level is nonzero:

   ```js
   smoothed = Math.floor((smoothed * 0.85 + target * 0.15) * 100) / 100;
   el.style.setProperty('--audio-scale', String(Math.max(1, 5 * smoothed)));
   ```

   Per display frame: an exponential moving average (0.85 old / 0.15 new), quantized to 0.01 steps (so the style only changes when the value moves), times a gain of 5, floored at 1. The loop self-stops when target and smoothed hit 0, and is gated on document visibility. Gain is 2 instead of 5 in meeting mode, which also switches to a time-based smoother with a slew limit of 1.6 units/s.

   Maximum bar height is therefore 2px x 5 x ~1 x 1.5 = 15px, fitting the 18px row.

## Meeting mode variant

In meeting (notetaker) mode the same keyframe list `[[0,1],[.2,1.2],[.4,1.5],[.8,1.1],[.9,1.3],[1,1]]` is evaluated in JavaScript instead (piecewise linear, via a rAF loop gated by an IntersectionObserver) and written per bar as `--wave-mult`; the bar is then drawn as a fixed 18px element clipped by `clip-path: inset(...)` to length `6px * audioScale * bulge * waveMult`, in the accent color. This avoids transform scaling for the always-on meeting chip.

## Mini waveform

A separate 5-bar "mini" waveform (used in the compact/resting indicator) hard-codes per-bar gains `[.8, 1, 1.2, 1, .8]` and delays `[.2, .3, .4, -.5, -.4]`, sets `--audio-scale: 5 * level` directly from props, and uses the same CSS animation idea with `--bar-level-gain` folded into the transform.

## Port to speecher

Speecher's `WaveformWidget` reimplements stages 2 and 3 plus the CSS model in QPainter: per-level adaptive dB floor mapping (fed from the existing `levelChanged` signal), a 150ms mean with hold-on-empty, a per-frame 0.85 EMA at a 16ms tick, gain 5 floored at 1, and per-bar `dotHeight x audioScale x bulge x wave` with the same keyframes, per-segment ease-in-out (Newton-solved bezier), and 0.1s/bar phase trail.

Three deliberate departures:

- **Colours are palette roles** (Base pill, Text bars) rather than Wispr Flow's fixed dark palette, because Speecher follows the desktop theme.
- **The pill is the transcript pill's size, 126x48.** Wispr Flow's active pill is 50x30 and stands alone against a screen edge. Speecher's popup stacks the waveform directly above the transcript pill, so at 50x30 it read as a separate, much smaller component. Taking that pill's size also means the pill never resizes between listening, the delivery receipt and the status shimmer.
- **Fifteen bars, not ten.** The bars keep Wispr Flow's thickness and spacing scaled by the pill's height ratio (3.2px wide, 3.2px gaps), so the wider pill holds proportionally more of the same bars instead of stretching them: fifteen fill 74% of 126px, the same fraction Wispr Flow's ten fill of 50px. Two consequences follow from the bar count:
  - The bulge uses Wispr Flow's linear branch (`1 - h * c/48`, the one its own `bulgeCoefficient >= 2` selects) rather than the quadratic. Over seven bars of distance the quadratic would flatten the outermost bars to nothing.
  - Each bar trails its neighbour by `1/barCount` of the loop rather than a literal 0.1s. Wispr Flow's 0.1s delay works because ten bars at 0.1s exactly fill its 1s loop; at fifteen a literal 0.1s would put bars 0 and 10 in lockstep and show two crests. At ten bars the expression reduces to Wispr Flow's own 0.1s.

The floor's clamp is the one constant that could not be carried over directly. Wispr Flow stops the floor descending past -60 dBFS of the raw capture; Speecher's level signal is pre-gained and the gain differs per audio input (`QtAudioInput` emits `rms*8` clipped at 1, `WavFileAudioInput` a unity peak), so there is no single dBFS equivalent. The port clamps at -46dB, which sits below room tone on both paths.
