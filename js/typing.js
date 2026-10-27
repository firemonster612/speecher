// Words land one at a time behind a moving caret, the way dictated text is typed into an app.

// Wrap each word of a paragraph in a span. Inline elements (code, .dim) land as one unit.
export function wrapWords(p) {
  const words = [];
  for (const child of [...p.childNodes]) {
    if (child.nodeType === Node.TEXT_NODE) {
      const frag = document.createDocumentFragment();
      for (const part of child.textContent.split(/(\s+)/)) {
        if (!part) continue;
        if (/^\s+$/.test(part)) { frag.append(part); continue; }
        const w = document.createElement("span");
        w.className = "w";
        w.textContent = part;
        frag.append(w);
        words.push(w);
      }
      child.replaceWith(frag);
    } else if (child.nodeType === Node.ELEMENT_NODE) {
      child.classList.add("w");
      words.push(child);
    }
  }
  return words;
}

// paras: paragraphs typed in order. speed scales the pace (1 is a brisk dictation paste).
export function createTypist(paras, { speed = 1 } = {}) {
  for (const p of paras) p.classList.add("type");
  const words = paras.flatMap(wrapWords);
  const caret = document.createElement("span");
  caret.className = "caret";
  caret.setAttribute("aria-hidden", "true");
  const times = [];
  let acc = 0;
  for (const w of words) {
    times.push(acc);
    const len = w.textContent.length;
    acc += (0.05 + 0.011 * len + (/[.,?]$/.test(w.textContent) ? 0.07 : 0)) / speed;
  }
  let start = null, shown = 0;

  const place = () => (shown ? words[shown - 1].after(caret) : paras[0].prepend(caret));
  return {
    duration: acc,
    get started() { return start !== null; },
    get done() { return start !== null && shown === words.length; },
    start(t) { start = t; caret.classList.remove("idle"); place(); },
    update(t) {
      if (start === null || shown === words.length) return;
      let moved = false;
      while (shown < words.length && t - start >= times[shown]) { words[shown++].classList.add("on"); moved = true; }
      if (moved) place();
      if (shown === words.length) caret.classList.add("idle");
    },
    // show everything at once, without the landing animation
    finish(keepCaret = true) {
      if (start === null) start = 0;
      for (const w of words) w.classList.add("on");
      shown = words.length;
      if (keepCaret) { place(); caret.classList.add("idle"); } else caret.remove();
    },
    hideCaret() { caret.remove(); },
    reset() { for (const w of words) w.classList.remove("on"); shown = 0; start = null; caret.remove(); },
  };
}

// Typing scrubbed by scroll: set(f) shows the first f of the words, with the caret after the last one.
export function createScrub(p) {
  p.classList.add("type");
  const words = wrapWords(p);
  const caret = document.createElement("span");
  caret.className = "caret";
  caret.setAttribute("aria-hidden", "true");
  let shown = -1, withCaret = null;
  return {
    set(f, caretOn = true) {
      const n = Math.round(Math.min(1, Math.max(0, f)) * words.length);
      if (n === shown && caretOn === withCaret) return;
      words.forEach((w, i) => w.classList.toggle("on", i < n));
      if (caretOn) {
        n ? words[n - 1].after(caret) : p.prepend(caret);
        caret.classList.toggle("idle", n === words.length);
      } else caret.remove();
      shown = n; withCaret = caretOn;
    },
  };
}
