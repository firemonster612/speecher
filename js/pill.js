// The dictation pill, recreated from the app's DictationPopup.qml, always dark
// (a system indicator over any app). One capsule that changes shape and
// content as a dictation moves through listening, refining and delivered, or
// opens into the review of an edit. While listening, its voice strip is the
// ridge range's P2 view; refining and delivered show no voice visual, as in
// the app. Sizes are the app's, in its px; the mount's --k scales the pill.
//
// Two ways to move between phases:
//   time:  set() eases the capsule over 300 ms (phone demos, the live page)
//   scrub: pose() puts the pill partway between two states from the scroll
//          position alone (no easing over time, no CSS transitions), so the
//          same position gives the same frame in both directions
(() => {
  const { Range, pillLook } = window.Ridge;
  const template = document.getElementById('pill-template');
  const BAR = 44, LINE = 22, BAND = 28, CONTENT_X = 44, MAX_LINES = 2, MAX_WORDS = 12;
  const MORPH = 300;   // the plan's pill phase morph
  // The plan's ease-out token, cubic-bezier(.165, .84, .44, 1).
  const easeOut = (x) => {
    if (x <= 0) return 0; if (x >= 1) return 1;
    let t = x;
    for (let i = 0; i < 8; i++) {
      const cx = 3 * .165 * t * (1 - t) ** 2 + 3 * .44 * t * t * (1 - t) + t ** 3 - x;
      const dx = 3 * .165 * (1 - t) ** 2 + 6 * (.44 - .165) * t * (1 - t) + 3 * (1 - .44) * t * t;
      if (Math.abs(cx) < 1e-5 || !dx) break;
      t = Math.min(1, Math.max(0, t - cx / dx));
    }
    return 3 * .84 * t * (1 - t) ** 2 + 3 * t * t * (1 - t) + t ** 3;
  };

  const clock = (s) => `${Math.floor(s / 60)}:${String(Math.floor(s % 60)).padStart(2, '0')}`;
  const escape = (s) => s.replace(/[&<>]/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;' })[c]);

  // A number that moves from one phase's value to the next.
  class Ease {
    constructor(v) { this.v = this.from = this.to = v; this.t = 1; }
    set(v, instant) {
      if (v === this.to && !instant) return;
      this.from = instant ? v : this.to; this.to = v; this.t = instant ? 1 : 0;
      if (instant) this.v = v;
    }
    step(dt) { this.t = Math.min(1, this.t + dt / MORPH); return this.at(this.t); }
    at(t) { this.v = this.from + (this.to - this.from) * easeOut(t); return this.v; }
  }

  class Pill {
    constructor(mount, voice, { scrub = false } = {}) {
      mount.append(template.content.cloneNode(true));
      mount.setAttribute('aria-hidden', 'true');
      this.mount = mount;
      this.el = mount.querySelector('.pill');
      const $ = (s) => this.el.querySelector(s);
      this.shell = $('.pill-shell');
      this.flow = $('.pill-flow');
      this.wordsBox = $('.pill-words');
      this.clockEl = $('.pill-clock');
      this.receiptOrb = $('.pill-receipt .orb');
      this.receiptTitle = $('.receipt-title');
      this.receiptDetail = $('.receipt-detail');
      this.receiptText = $('.receipt-text');
      this.review = $('.pill-review');
      this.canvas = $('.pill-ink');
      this.voice = voice; this.range = null;
      this.scrub = scrub; this.blend = 1; this.pair = '';
      this.el.classList.toggle('scrub', scrub);
      this.spans = []; this.lines = 0; this.phase = 'hidden'; this.key = '';
      this.w = new Ease(280); this.h = new Ease(BAR); this.strip = new Ease(1);
      this.visible = true;
      this.measure();
    }

    // --k scales the pill; --pill-wide is the widest live capsule (the app's
    // 460), which phones narrow.
    measure() {
      const style = getComputedStyle(this.mount);
      this.k = parseFloat(style.getPropertyValue('--k')) || 1;
      this.wide = parseFloat(style.getPropertyValue('--pill-wide')) || 460;
      // DictationPopup.qml makes the review at least 500 wide; phones narrow it
      // (with wrapped actions) rather than shrinking its text.
      this.reviewWide = parseFloat(style.getPropertyValue('--review-wide')) || 500;
      this.flow.style.width = `${this.wide - CONTENT_X - 18}px`;
      this.density = Math.min(3, (devicePixelRatio || 1) * this.k);
      if (this.range) { this.range.look = pillLook(this.density); this.range.fit(this.density); }
      this.key = this.pair = '';
    }
    // The GL context only when the strip is first drawn: receipts never need one.
    ensureRange() {
      if (!this.range) { this.range = new Range(this.canvas, this.voice, pillLook(this.density)); this.range.fit(this.density); }
      return this.range;
    }

    // state: { phase: hidden | listening | refining | delivered | review,
    //   words: [..], count, seconds, receipt: { mark, title, detail },
    //   review: { said, summary, segments: [{ kind, text }], pressed } }
    set(state) {
      const key = JSON.stringify(state);
      if (key === this.key) return;
      this.key = key;
      const { phase, words = [], count = words.length, seconds = 0 } = state;
      const instant = this.phase === 'hidden';
      this.phase = phase;
      this.el.dataset.phase = phase;
      this.setWords(words, phase === 'listening' || phase === 'refining' ? count : 0);
      this.flow.classList.toggle('shimmer', phase === 'refining');
      this.clockEl.textContent = clock(seconds);
      if (state.receipt) this.setReceipt(state.receipt);
      if (state.review) this.setReview(state.review);
      const { w, h } = this.target(phase);
      this.w.set(w, instant); this.h.set(h, instant);
      this.strip.set(phase === 'listening' ? 1 : 0, instant);
    }

    // Scrub: the pill at t (0..1) of the way from one state to another. The
    // shell morphs; the content switches whole at cut, never half faded, so
    // the pill is readable wherever the scroll stops. A cut of 1 keeps the
    // outgoing content until the shell has its new size (the review). Both
    // ends' sizes are measured once per pair.
    pose(from, to, t, { cut = .5 } = {}) {
      const pair = JSON.stringify([from, to]);
      if (pair !== this.pair) {
        this.set(from); const a = this.target(from.phase);
        this.set(to); const b = this.target(to.phase);
        this.ends = { a, b };
        this.pair = pair;
      }
      const { a, b } = this.ends;
      this.set(t >= cut ? to : from);
      const strip = (s) => (s.phase === 'listening' ? 1 : 0);
      [this.w.from, this.w.to, this.h.from, this.h.to, this.strip.from, this.strip.to] = [a.w, b.w, a.h, b.h, strip(from), strip(to)];
      this.blend = t;
    }

    // The app's LiveWords: each new word rises into place, older words dim,
    // and only the last two lines show.
    setWords(words, count) {
      while (this.spans.length > count) this.spans.pop().remove();
      for (let i = 0; i < count; i++) {
        let span = this.spans[i];
        if (!span || span.textContent !== words[i]) {
          if (span) span.remove();
          span = document.createElement('span');
          span.className = 'w';
          span.textContent = words[i];
          span.style.setProperty('--i', String(i));
          this.flow.insertBefore(span, this.spans[i + 1] ?? null);
          this.spans[i] = span;
          if (this.scrub || document.documentElement.classList.contains('rm')) span.classList.add('in');
          else requestAnimationFrame(() => requestAnimationFrame(() => span.classList.add('in')));
        }
        span.style.setProperty('--age', String(1 - Math.min(1, (count - 1 - i) / MAX_WORDS) * 0.62));
      }
      this.lines = count ? Math.min(MAX_LINES, Math.round(this.flow.offsetHeight / LINE)) : 0;
      this.wordsBox.style.height = `${this.lines * LINE}px`;
    }

    // The receipt's title is the delivery's first message (QuickDictation.cpp),
    // so a warning is the title itself, wrapped.
    setReceipt({ mark = 'delivered', title, detail = '' }) {
      this.receiptOrb.dataset.mark = mark;
      this.receiptTitle.textContent = title;
      this.receiptDetail.textContent = detail;
      this.receiptDetail.hidden = !detail;
    }

    // DiffText: added words in the accent on a faint wash, removed ones struck,
    // each run with its own spacing.
    setReview({ said, summary, segments, pressed = false }) {
      this.review.querySelector('.review-said').textContent = `You said ${said}`;
      this.review.querySelector('.review-summary').textContent = summary;
      this.review.querySelector('.review-diff').innerHTML = segments.map(({ kind, text }) =>
        kind === 'added' ? `<ins>${escape(text)}</ins>` : kind === 'removed' ? `<del>${escape(text)}</del>` : escape(text)).join('');
      this.review.querySelector('.review-replace').classList.toggle('pressed', pressed);
    }

    // The capsule's size for a phase, as the app's target.
    target(phase) {
      let width = 280, height = BAR;
      const text = this.lines > 0;
      if (phase === 'listening') width = text ? this.wide : 280;
      else if (phase === 'refining') width = text ? this.wide : 220;
      if ((phase === 'listening' || phase === 'refining') && text) height = BAR + 12 + this.lines * LINE - 4;
      if (phase === 'delivered') {
        width = Math.min(this.wide + 40, Math.max(160, 10 + 24 + 10 + this.receiptText.scrollWidth + 18));
        height = Math.max(BAR, this.receiptText.offsetHeight + 20);
      }
      if (phase === 'review') { width = this.reviewWide; height = 24 + this.review.querySelector('.review-body').offsetHeight; }
      return { w: width, h: height };
    }

    // The shell's rect in the viewport, now.
    rect() { return this.shell.getBoundingClientRect(); }
    // The strip in pill px: from the orb's place to the clock, on the bar's centre line.
    stripBox() { return { x0: 14, x1: this.w.v - 118, cy: this.h.v - BAR / 2, h: BAND }; }

    apply() {
      this.shell.style.width = `${this.w.v.toFixed(2)}px`;
      this.shell.style.height = `${this.h.v.toFixed(2)}px`;
    }

    frame(dt) {
      if (this.scrub) for (const e of [this.w, this.h, this.strip]) e.at(this.blend);
      else for (const e of [this.w, this.h, this.strip]) e.step(dt);
      this.apply();
      this.draw();
    }

    draw(breath = .5 + .5 * Math.sin(performance.now() / 1000 * 1.85)) {
      const alpha = this.strip.v;
      // The still lens (no WebGL) follows the strip: listening only.
      this.canvas.style.opacity = this.canvas.classList.contains('p2-still') ? alpha.toFixed(3) : '';
      if (!this.visible || alpha <= .001) {
        // Clear once, then submit nothing while hidden.
        if (!this.cleared) { this.range?.clear(); this.cleared = true; }
        return;
      }
      this.cleared = false;
      const range = this.ensureRange();
      // Without WebGL, a still lens stands in for the strip.
      if (range.failed || range.lost) { this.canvas.classList.add('p2-still'); return; }
      range.alpha = alpha;
      // The canvas spans the bar: 8 px in from the shell's left, 448 wide.
      const s = this.stripBox();
      range.layout(s.x0 - 8, BAR / 2, Math.max(1, s.x1 - s.x0), BAND);
      range.render({ breath });
    }

    // Reduced motion: the final shape at once, one still draw, no frame loop.
    settle() {
      for (const e of [this.w, this.h, this.strip]) e.at(1);
      this.apply();
      this.draw(1);
    }
  }

  window.Pill = Pill;
})();
