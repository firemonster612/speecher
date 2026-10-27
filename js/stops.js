// The scroll stops after the intro. Each scene is scrubbed by scroll: p is progress through the
// stop's hold (0 to 1) and e is progress through the screen of scroll that brings it in.
// Each stop also tells main.js where the pill is, how loud it is, and what it carries.

import { createScrub } from "./typing.js";
import { talk, capOrbit } from "./scene.js";

const S = window.SPEECHER_SAMPLES;
// A resting pill murmurs: short, uneven bars, not a row of dots that reads as a loading indicator.
export const idle = (t) => 0.1 + 0.3 * talk(t * 0.45);
const clamp = (x) => Math.min(1, Math.max(0, x));
const ease = (k) => { k = clamp(k); return k < 0.5 ? 4 * k * k * k : 1 - Math.pow(-2 * k + 2, 3) / 2; };
const speaking = (t) => 0.2 + talk(t) * 0.75;

export function createStops({ reduced }) {
  const sec = [...document.querySelectorAll(".stop")];

  // 1. plan: the two accounts sit in the pill's caps, then trade places
  const plan = sec[1];
  const planSlot = plan.querySelector(".slot");
  const accts = plan.querySelector(".accts");
  const acctEls = [...accts.querySelectorAll(".acct")];
  const strikes = [...plan.querySelectorAll(".nope path")];
  let planBox = null, swap = 0;

  // 2. words: one dictated sentence, corrected word by word, then kubectl drops into the list
  const words = sec[2];
  const said = words.querySelector(".said");
  const tokens = [];
  for (const raw of S.agent.raw.replace(S.agent.learned.heard, "\u0000").split(" ")) {
    const el = document.createElement("span");
    el.className = "tk";
    if (raw === "\u0000") {
      el.classList.add("fix");
      el.innerHTML = `${S.agent.learned.heard}<ins>${S.agent.learned.fixed}</ins>`;
    } else el.textContent = raw;
    said.append(" ", el);
    tokens.push(el);
  }
  const first = tokens[0], firstRaw = first.textContent;
  tokens.at(-1).insertAdjacentHTML("beforeend", '<span class="end">.</span>');
  said.setAttribute("aria-label", S.agent.refined.replace(/`/g, ""));
  const learned = words.querySelector(".chip.learned");
  const fixIns = words.querySelector(".fix ins");
  let drop = { x: 0, y: 0 }, sweeping = false;

  // 3. native: two apps stand on your OS at the same height, then a web page and a browser engine
  // rise under the browser-based one and jack it up. Speecher's stack stays as it is.
  const native = sec[3];
  const appSlab = native.querySelector(".slab.app");
  const inserts = [native.querySelector(".slab.engine"), native.querySelector(".slab.script")];
  let rise = 0;

  // 4. android: the phone rises, the pill is its keyboard's button, the text lands in the field
  const android = sec[4];
  const phone = android.querySelector(".phone");
  const keys = android.querySelector(".keys");
  for (const n of [10, 9, 9, 3]) {
    const row = document.createElement("div");
    row.className = "row";
    row.append(...Array.from({ length: n }, () => document.createElement("i")));
    keys.append(row);
  }
  const field = android.querySelector(".field .text");
  field.textContent = S.chat.refined;
  const fieldText = createScrub(field);
  let phoneP = 1;

  // 5. download: the pill becomes the button for this platform, the other platforms unfold under it.
  // The button takes over this platform's row, so the row leaves the list.
  const download = sec[5];
  const mainDl = download.querySelector(".main-dl");
  const ua = navigator.userAgent;
  const os = /Android/.test(ua) ? "android" : /Mac/.test(ua) ? "mac" : /Win/.test(ua) ? "windows" : "linux";
  const mine = download.querySelector(`[data-os="${os}"]`);
  mainDl.href = mine.querySelector("a").href;
  mainDl.querySelector("b").textContent = mine.querySelector("b").textContent;
  download.querySelector(".dl-req").textContent = mine.querySelector("span").textContent;
  mine.remove();
  const rows = [...download.querySelectorAll(".rows li")];
  const dlReq = download.querySelector(".dl-req");
  let dlE = 1;

  // Sizes that don't change with scroll. Called on load and resize.
  function measure() {
    const ar = accts.getBoundingClientRect(), sr = planSlot.getBoundingClientRect();
    planBox = { x: sr.left + sr.width / 2 - ar.left, y: sr.bottom - ar.top, w: sr.width, h: sr.height, lw: acctEls.map((el) => el.offsetWidth) };
    learned.style.transform = "";
    const a = fixIns.getBoundingClientRect(), b = learned.getBoundingClientRect();
    drop = { x: a.left + a.width / 2 - (b.left + b.width / 2), y: a.top + a.height / 2 - (b.top + b.height / 2) };
    for (const el of [appSlab, ...inserts]) el.style.transform = "";
    rise = inserts[0].getBoundingClientRect().top - inserts[1].getBoundingClientRect().top;
  }

  const scenes = [null,
    // plan: red pencil first, then the swap
    (p) => {
      if (reduced) p = 0;
      strikes.forEach((path, j) => {
        const k = reduced ? 1 : ease((p - 0.02 - 0.1 * j) / 0.2);
        path.style.strokeDashoffset = 1 - k;
        path.classList.toggle("off", k < 0.01);
      });
      swap = ease((p - 0.34) / 0.42);
      const o = capOrbit(planBox.w, planBox.h, swap);
      acctEls.forEach((el, j) => {
        const away = Math.abs(o[j].y) / (planBox.h * 0.62);
        el.style.transform = `translate(${planBox.x + o[j].x - planBox.lw[j] / 2}px, ${planBox.y + planBox.h * 0.12}px)`;
        el.style.opacity = clamp(1 - away * 1.8);
      });
    },
    // words: the sweep, then the drop
    (p) => {
      if (reduced) p = 1;
      sweeping = p < 0.62;
      tokens.forEach((el, j) => el.classList.toggle("on", p > 0.04 + (0.56 * (j + 0.5)) / tokens.length));
      first.textContent = first.classList.contains("on") ? firstRaw[0].toUpperCase() + firstRaw.slice(1) : firstRaw;
      const k = clamp((p - 0.68) / 0.26);
      learned.classList.toggle("flying", k > 0 && k < 1);
      learned.classList.toggle("landed", k >= 1);
      learned.style.opacity = k > 0 ? 1 : 0;
      // across at an even pace, down like a dropped thing
      learned.style.transform = k > 0 && k < 1 ? `translate(${drop.x * (1 - ease(k))}px, ${drop.y * (1 - k * k)}px) rotate(${(1 - k) * -6}deg)` : "";
    },
    // native: the web page rises out of your OS under the app, then the browser engine rises under both.
    // Each rises from behind the OS slab, so the stack has no gap wherever the scroll stops.
    (p) => {
      if (reduced) return;
      const page = ease((p - 0.08) / 0.34), engine = ease((p - 0.5) / 0.34);
      const down = (n) => (n > 0 ? `translateY(${n * rise}px)` : "");
      inserts[0].style.transform = down(1 - engine);
      inserts[1].style.transform = appSlab.style.transform = down(2 - page - engine);
    },
    // android: rise and straighten, then type
    (p) => {
      if (reduced) p = 1;
      phoneP = p;
      const r = ease(p / 0.3);
      phone.style.transform = r < 1 ? `translateY(${(1 - r) * 6}%) rotate(${(1 - r) * -6}deg) scale(${1 - (1 - r) * 0.08})` : "";
      fieldText.set((p - 0.46) / 0.42, true);
    },
    // download: the button and the rows
    (p, e) => {
      dlE = reduced ? 1 : e;
      mainDl.style.opacity = dlReq.style.opacity = clamp((dlE - 0.8) / 0.16);
      rows.forEach((li, j) => {
        const k = ease((dlE - 0.5 - j * 0.06) / 0.26);
        li.style.opacity = k;
        li.style.transform = k < 1 ? `translateY(${(1 - k) * -(j + 1) * 50}%)` : "";
      });
    },
  ];

  const states = [null,
    (t) => ({ el: planSlot, level: reduced ? 0.5 : Math.max(0.2 + talk(t) * 0.6, Math.sin(Math.PI * swap) * 0.95), caps: 1, swap }),
    (t) => ({ el: words.querySelector(".slot"), level: reduced ? 0.45 : sweeping ? speaking(t) : idle(t) }),
    (t) => ({ el: native.querySelector(".slot"), level: reduced ? 0.45 : idle(t) }),
    (t) => {
      const tap = phoneP > 0.32 && phoneP < 0.42 ? 0.88 : 1;
      const live = phoneP > 0.36 && phoneP < 0.9;
      return { el: android.querySelector(".slot"), level: reduced ? 0.45 : live ? speaking(t) : idle(t), s: tap, rot: (1 - ease(phoneP / 0.3)) * -6 * Math.PI / 180 };
    },
    (t) => ({ el: mainDl, level: reduced ? 0.45 : idle(t), a: 1 - clamp((dlE - 0.84) / 0.14) }),
  ];

  return {
    states,
    measure,
    update(i, p, e) { scenes[i]?.(p, e); },
  };
}
