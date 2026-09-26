// Speecher Home with insights: web mockup.
// Generates a year of fake Dictation Sessions from a fixed seed and derives
// every number on the page from them, so the stats agree with each other.

const TODAY = new Date(2026, 8, 26); // Sat 26 Sep 2026
const DAY = 86400000;
const TYPING_WPM = 40;
// Word counts: Shakespeare from Open Source Shakespeare, novels from Nathan
// Bransford's novel word count list, the Gettysburg Address from the Bliss copy.
const BOOKS = [
  { title: "<i>Macbeth</i>", words: 17121 },
  { title: "<i>Romeo and Juliet</i>", words: 24545 },
  { title: "<i>Hamlet</i>", words: 30557 },
  { title: "<i>The Great Gatsby</i>", words: 47094 },
  { title: "the first <i>Harry Potter</i> book", words: 76944 },
  { title: "<i>The Hobbit</i>", words: 95356 },
  { title: "<i>To Kill a Mockingbird</i>", words: 100388 },
  { title: "<i>The Fellowship of the Ring</i>", words: 187790 },
  { title: "<i>Moby-Dick</i>", words: 209117 },
  { title: "<i>War and Peace</i>", words: 561304 },
];
const GETTYSBURG_WORDS = 272;
// Plain fractions and multiples only. Half, whole and twice read easiest, so
// the others need to fit noticeably better to win.
const SHARES = [
  { of: 1 / 4, say: "a quarter of", cost: 0.2 },
  { of: 1 / 3, say: "a third of", cost: 0.2 },
  { of: 1 / 2, say: "half of", cost: 0 },
  { of: 3 / 4, say: "three-quarters of", cost: 0.2 },
  { of: 1, say: "as long as", cost: 0 },
  { of: 2, say: "twice the length of", cost: 0 },
  { of: 3, say: "three times the length of", cost: 0.2 },
  { of: 4, say: "four times the length of", cost: 0.2 },
];
const MILESTONES = [1000, 10000, 50000, 100000, 250000, 500000, 1000000];

const APPS = [
  { name: "Slack", profile: "Work", weight: 30 },
  { name: "Claude Code", profile: "AI coding", weight: 26 },
  { name: "Thunderbird", profile: "Email", weight: 15 },
  { name: "Obsidian", profile: "Personal", weight: 11 },
  { name: "Firefox", profile: "Other", weight: 9 },
  { name: "Kate", profile: "Other", weight: 4 },
  { name: "Signal", profile: "Personal", weight: 3 },
  { name: "LibreOffice Writer", profile: "Work", weight: 2 },
];
const FILLERS = [["um", 34], ["uh", 22], ["like", 19], ["you know", 11], ["so", 9], ["I mean", 5]];
const HOUR_WEIGHTS = [0, 0, 0, 0, 0, 0, 1, 3, 7, 12, 14, 12, 7, 6, 10, 12, 11, 8, 4, 3, 4, 5, 3, 1];

const RANGE_NOUN = { 7: "week", 30: "30 days" };
const params = new URLSearchParams(location.search);
const state = {
  scenario: params.get("state") || "active",
  theme: params.get("theme") || "dark",
  accent: params.get("accent") || "blue",
  width: params.get("width") || "wide",
  range: "30",
  heatMetric: "count",
  dictation: "idle", // idle | listening
  page: params.get("page") || "home", // home | general
  insights: params.get("insights") !== "off",
  confirmClear: false,
};

// ------------------------------------------------------------------ data

function rng(seed) {
  return () => {
    seed |= 0; seed = (seed + 0x6d2b79f5) | 0;
    let t = Math.imul(seed ^ (seed >>> 15), 1 | seed);
    t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
}

function pickWeighted(random, items, weightOf) {
  const total = items.reduce((sum, item, i) => sum + weightOf(item, i), 0);
  let roll = random() * total;
  for (let i = 0; i < items.length; i++) {
    roll -= weightOf(items[i], i);
    if (roll <= 0) return i;
  }
  return items.length - 1;
}

const dayIndex = (date) => Math.round((startOfDay(date) - startOfDay(TODAY)) / DAY); // 0 = today, negative = past
const startOfDay = (date) => new Date(date.getFullYear(), date.getMonth(), date.getDate());
const dateOf = (index) => new Date(TODAY.getTime() + index * DAY);

function makeSession(random, day) {
  const hour = pickWeighted(random, HOUR_WEIGHTS, (w) => w);
  const seconds = Math.round(6 + Math.exp(3.1 + random() * 1.6) * (0.6 + random()));
  const wpm = 120 + random() * 50;
  const words = Math.max(3, Math.round((seconds / 60) * wpm));
  const app = APPS[pickWeighted(random, APPS, (a) => a.weight)].name;
  const fillers = Math.round(words * (0.015 + random() * 0.025));
  return { day, hour, minute: Math.floor(random() * 60), seconds, words, app, fillers };
}

function generate(scenario) {
  const random = rng(4242);
  const sessions = [];
  if (scenario === "new") {
    for (let i = 0; i < 4; i++) sessions.push(makeSession(random, 0));
    return sessions;
  }
  const first = -359;
  const recentRun = scenario === "lapsed" ? [-26, -4] : [-23, 0];
  for (let day = first; day <= 0; day++) {
    const date = dateOf(day);
    const weekend = date.getDay() === 0 || date.getDay() === 6;
    const holiday = date >= new Date(2025, 11, 22) && date <= new Date(2026, 0, 3);
    const adoption = Math.min(1, 0.35 + (day - first) / 140);
    let active = random() < (weekend ? 0.42 : 0.9) * adoption && !holiday;
    if (day >= recentRun[0] && day <= recentRun[1]) active = true;
    if (day === recentRun[0] - 1 || day > recentRun[1]) active = false;
    if (day === -208) active = true; // the busiest day on record
    if (!active) continue;
    let count = Math.max(1, Math.round((weekend ? 3 : 9) * adoption * (0.4 + random() * 1.4)));
    if (day === -208) count = 37;
    for (let i = 0; i < count; i++) sessions.push(makeSession(random, day));
  }
  return sessions.sort((a, b) => a.day - b.day || a.hour - b.hour || a.minute - b.minute);
}

let sessions = generate(state.scenario);

// ------------------------------------------------------------------ stats

const sum = (list, key) => list.reduce((total, s) => total + s[key], 0);

function byDay(list) {
  const days = new Map();
  for (const s of list) {
    const d = days.get(s.day) || { count: 0, words: 0, seconds: 0 };
    d.count++; d.words += s.words; d.seconds += s.seconds;
    days.set(s.day, d);
  }
  return days;
}

function streaks(days) {
  // A streak survives until the end of today, so an idle today doesn't break it.
  let current = 0;
  for (let d = days.has(0) ? 0 : -1; days.has(d); d--) current++;
  let best = 0, run = 0, bestEnd = 0;
  const firstDay = Math.min(...days.keys());
  for (let d = firstDay; d <= 0; d++) {
    run = days.has(d) ? run + 1 : 0;
    if (run > best) { best = run; bestEnd = d; }
  }
  let lastEnded = null;
  if (current === 0) {
    const lastActive = Math.max(...days.keys());
    let length = 0;
    for (let d = lastActive; days.has(d); d--) length++;
    lastEnded = { day: lastActive, length };
  }
  return { current, best, bestEnd, lastEnded };
}

function rangeBounds(range) {
  if (range === "all") return [-Infinity, 0];
  if (range === "year") return [dayIndex(new Date(TODAY.getFullYear(), 0, 1)), 0];
  const n = Number(range);
  return [-(n - 1), 0];
}

function inRange(list, [from, to]) {
  return list.filter((s) => s.day >= from && s.day <= to);
}

// ------------------------------------------------------------------ format

const nf = new Intl.NumberFormat("en");
const fmt = (n) => nf.format(Math.round(n));
const plural = (n, one, many = one + "s") => `${fmt(n)} ${n === 1 ? one : many}`;
const dateFmt = new Intl.DateTimeFormat("en", { month: "short", day: "numeric", year: "numeric" });
const dayFmt = new Intl.DateTimeFormat("en", { weekday: "short", month: "short", day: "numeric", year: "numeric" });
const weekdayFmt = new Intl.DateTimeFormat("en", { weekday: "long" });

function duration(seconds) {
  if (seconds < 60) return `${Math.round(seconds)}s`;
  const minutes = Math.round(seconds / 60);
  if (minutes < 60) return `${minutes} min`;
  const h = Math.floor(minutes / 60), m = minutes % 60;
  return m ? `${h} h ${m} min` : `${h} h`;
}
function clock(seconds) {
  return `${Math.floor(seconds / 60)}:${String(seconds % 60).padStart(2, "0")}`;
}
function hourLabel(h) {
  const suffix = h < 12 ? "am" : "pm";
  return `${h % 12 === 0 ? 12 : h % 12} ${suffix}`;
}
function relativeDay(day) {
  if (day === 0) return "today";
  if (day === -1) return "yesterday";
  if (day > -7) return weekdayFmt.format(dateOf(day));
  return dateFmt.format(dateOf(day));
}
// "About half of Hamlet": the book and plain fraction closest to the count.
function wordsAsBook(words) {
  const smallest = BOOKS[0].words * SHARES[0].of;
  if (words < smallest) {
    const addresses = Math.round(words / GETTYSBURG_WORDS);
    const tip = `The Gettysburg Address is ${GETTYSBURG_WORDS} words`;
    if (words < GETTYSBURG_WORDS / 4) return { text: "A few sentences so far" };
    if (words < GETTYSBURG_WORDS * 0.75) return { text: "About half the Gettysburg Address", tip };
    return { text: addresses === 1 ? "About the Gettysburg Address" : `About ${addresses} Gettysburg Addresses`, tip };
  }
  let best;
  for (const book of BOOKS) {
    for (const share of SHARES) {
      const score = Math.abs(Math.log(words / (book.words * share.of))) + share.cost;
      if (!best || score < best.score) best = { score, book, share };
    }
  }
  const last = BOOKS[BOOKS.length - 1];
  if (words > last.words * 5) best = { book: last, share: { say: `${Math.round(words / last.words)} times the length of` } };
  return {
    text: `About ${best.share.say} ${best.book.title}`,
    tip: `${best.book.title.replace(/^the /, "The ")} is about ${fmt(best.book.words)} words`,
  };
}
function delta(current, previous) {
  if (!previous) return "";
  const change = Math.round(((current - previous) / previous) * 100);
  if (change === 0) return `same as previous ${RANGE_NOUN[state.range]}`;
  return `${change > 0 ? "▲" : "▼"} ${Math.abs(change)}% vs previous ${RANGE_NOUN[state.range]}`;
}

const esc = (s) => String(s).replace(/[&<>"]/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" })[c]);
const icon = (path) => `<svg class="icon" viewBox="0 0 16 16" aria-hidden="true"><path d="${path}"/></svg>`;
const ICONS = {
  words: "M3 3.5h10M3 6.5h10M3 9.5h10M3 12.5h6",
  flame: "M8 14c-2.8 0-4.5-1.8-4.5-4.3C3.5 6.5 7 5.5 7 2c2.5 1.5 5.5 4.3 5.5 7.7C12.5 12.2 10.8 14 8 14zM8 14c-1.2 0-2-.8-2-2 0-1.4 1.3-2 2-3.5.8 1.5 2 2.1 2 3.5 0 1.2-.8 2-2 2z",
  mic: "M8 2a2 2 0 012 2v4a2 2 0 01-4 0V4a2 2 0 012-2zM4 7.5a4 4 0 008 0M8 11.5V14",
  wave: "M2 8h1.5M5 5v6M8 3v10M11 5.5v5M14 8h-1",
  copy: "M5.5 5.5h7v8h-7zM3.5 10.5v-8h7",
  check: "M3 8.5l3 3 7-7",
  lock: "M4.5 7.5h7v6h-7zM6 7.5V5.5a2 2 0 014 0v2",
  chevron: "M6 3.5L10.5 8 6 12.5",
};

// ------------------------------------------------------------------ render

function render() {
  document.getElementById("page-title").textContent = state.page === "home" ? "Home" : "General";
  for (const item of document.querySelectorAll(".sidebar li")) {
    item.classList.toggle("current", item.dataset.page === state.page);
  }
  const column = document.getElementById("column");
  if (state.page === "general") {
    column.innerHTML = generalPage();
    return;
  }
  if (!state.insights || !sessions.length) {
    column.innerHTML = dictateCard() + insightsUnavailableCard();
    return;
  }
  const days = byDay(sessions);
  const streak = streaks(days);
  column.innerHTML = [
    dictateCard(),
    `<h2 class="section-title card-head" style="margin-bottom:-2px">
       <span>Your dictation</span>
       <select class="combo" data-bind="range" aria-label="Period">
         ${[["7", "Last 7 days"], ["30", "Last 30 days"], ["year", "This year"], ["all", "All time"]]
           .map(([v, l]) => `<option value="${v}" ${state.range === v ? "selected" : ""}>${l}</option>`).join("")}
       </select>
     </h2>`,
    tiles(days, streak),
    heatmapCard(days),
    `<div class="row2">${hoursCard()}${paceCard()}</div>`,
    `<div class="row2">${appsCard()}${cleanupCard()}</div>`,
    `<h2 class="section-title">Records</h2>`,
    recordsCard(days, streak),
    `<p class="privacy muted small">${icon(ICONS.lock)}<span>Insights are stored only on this computer and are never sent to the cloud.
      <a href="#" data-action="open-general">Insights settings</a></span></p>`,
  ].join("");
  drawHeatmap(days);
  drawHours();
}

function dictateCard() {
  const last = sessions[sessions.length - 1];
  const listening = state.dictation === "listening";
  const lastText = "Can you move the design review to Thursday afternoon? I want to walk through the new home page with the insights cards before we start on the native ports.";
  return `<section class="card dictate" aria-label="Dictation">
    <div class="status">${listening ? `<span class="live"></span>Listening` : "Idle"}
      ${listening ? `<span class="wave">${"<i></i>".repeat(7).replace(/<i>/g, (m, i) => `<i style="animation-delay:-${(i % 5) * 0.13}s">`)}</span>` : ""}</div>
    <div class="hint muted">${listening ? "Speak, then press the shortcut again to paste." : `Press <kbd>Meta</kbd> <kbd>Space</kbd> anywhere to dictate into the app you're using.`}</div>
    <div class="actions">
      <button class="button" data-action="toggle">
        ${listening ? `<span class="square"></span>Stop Dictation` : `<span class="dot"></span>Start Dictation`}
      </button>
    </div>
    ${last ? `<div class="last">
      <p>${esc(lastText)}</p>
      <span class="meta muted small">${lastText.split(" ").length} words, ${esc(last.app)}, ${relativeDay(last.day)}</span>
      <button class="tool-button" data-action="copy" data-tip="Copy transcript" aria-label="Copy transcript">${icon(ICONS.copy)}</button>
    </div>` : ""}
  </section>`;
}

function tiles(days, streak) {
  const bounds = rangeBounds(state.range);
  const current = inRange(sessions, bounds);
  const span = bounds[1] - bounds[0] + 1;
  const previous = Number.isFinite(span) && state.range !== "year"
    ? inRange(sessions, [bounds[0] - span, bounds[0] - 1]) : [];
  const words = sum(current, "words");
  const seconds = sum(current, "seconds");
  const activeDays = new Set(current.map((s) => s.day)).size;

  const weekStart = -((TODAY.getDay() + 6) % 7); // Monday of this week
  const weekDots = ["M", "T", "W", "T", "F", "S", "S"].map((label, i) => {
    const day = weekStart + i;
    const cls = [days.has(day) ? "on" : "", day === 0 ? "today" : ""].join(" ");
    return day > 0 ? `<span>${label}</span>` : `<span class="${cls}" data-tip="${esc(dayFmt.format(dateOf(day)))}${days.has(day) ? `<br>${plural(days.get(day).count, "dictation")}` : "<br>No dictation"}">${label}</span>`;
  }).join("");

  let streakSub;
  if (streak.current > 0) {
    streakSub = streak.current >= streak.best ? "Your longest yet" : `Best: ${plural(streak.best, "day")}`;
    if (!days.has(0)) streakSub = "Dictate today to keep it going";
  } else if (streak.lastEnded) {
    streakSub = `${streak.lastEnded.length}-day run ended ${relativeDay(streak.lastEnded.day + 1)}`;
  }

  return `<div class="tiles">
    <div class="card tile">
      <span class="label">${icon(ICONS.words)}Words dictated</span>
      <span class="value num">${fmt(words)}</span>
      ${(() => {
        if (!words) return `<span class="sub">Nothing yet</span>`;
        const book = wordsAsBook(words);
        return `<span class="sub"${book.tip ? ` data-tip="${esc(book.tip)}"` : ""}>${book.text}</span>`;
      })()}
      ${previous.length ? `<span class="sub">${delta(words, sum(previous, "words"))}</span>` : ""}
    </div>
    <div class="card tile">
      <span class="label">${icon(ICONS.flame)}Streak</span>
      <span class="value num">${streak.current}<small>${streak.current === 1 ? "day" : "days"}</small></span>
      <span class="sub">${streakSub}</span>
      <div class="week-dots">${weekDots}</div>
    </div>
    <div class="card tile">
      <span class="label">${icon(ICONS.mic)}Dictations</span>
      <span class="value num">${fmt(current.length)}</span>
      <span class="sub">${activeDays ? `${(current.length / activeDays).toFixed(1)} a day when you dictate` : "Nothing yet"}</span>
      ${previous.length ? `<span class="sub">${delta(current.length, previous.length)}</span>` : ""}
    </div>
    <div class="card tile">
      <span class="label">${icon(ICONS.wave)}Audio transcribed</span>
      <span class="value num">${seconds >= 3600 ? (seconds / 3600).toFixed(1) : Math.round(seconds / 60)}<small>${seconds >= 3600 ? "hours" : "min"}</small></span>
      <span class="sub">${current.length ? `Average dictation ${clock(Math.round(seconds / current.length))}` : "Nothing yet"}</span>
    </div>
  </div>`;
}

// ---------------------------------------------------------------- heatmap

function heatmapCard(days) {
  const yearAgo = -364;
  const active = [...days.keys()].filter((d) => d >= yearAgo).length;
  return `<section class="card heatmap">
    <div class="card-head">
      <h3>Activity</h3>
      <select class="combo" data-bind="heatMetric" aria-label="Measure">
        ${[["count", "Dictations"], ["words", "Words"], ["seconds", "Minutes of audio"]]
          .map(([v, l]) => `<option value="${v}" ${state.heatMetric === v ? "selected" : ""}>${l}</option>`).join("")}
      </select>
    </div>
    <div id="heatmap"></div>
    <div class="heat-foot">
      <span class="muted small">${plural(active, "day")} with dictation in the last year</span>
      <span class="legend small muted"><span>Less</span><i class="l0"></i><i class="l1"></i><i class="l2"></i><i class="l3"></i><i class="l4"></i><span>More</span></span>
    </div>
  </section>`;
}

function drawHeatmap(days) {
  const host = document.getElementById("heatmap");
  const labelW = 28, labelH = 16, gap = 3;
  const width = host.clientWidth;
  // Keep cells at least 10px: a narrow window shows fewer weeks, not smaller cells.
  let weeks = 53;
  let cell = Math.min(14, (width - labelW) / weeks - gap);
  if (cell < 10) { cell = 10; weeks = Math.floor((width - labelW) / (cell + gap)); }
  const pitch = cell + gap;

  const values = [...days.values()].map((d) => d[state.heatMetric]).sort((a, b) => a - b);
  const q = (p) => values[Math.floor(p * (values.length - 1))] ?? 0;
  const cuts = [q(0.25), q(0.5), q(0.75)];
  const level = (v) => (!v ? 0 : v <= cuts[0] ? 1 : v <= cuts[1] ? 2 : v <= cuts[2] ? 3 : 4);

  const todayRow = (TODAY.getDay() + 6) % 7; // Monday = 0
  const firstDay = -((weeks - 1) * 7 + todayRow);
  let cells = "", months = "";
  let lastMonth = -1;
  for (let w = 0; w < weeks; w++) {
    const monthDay = dateOf(firstDay + w * 7);
    if (monthDay.getMonth() !== lastMonth && w < weeks - 2) {
      if (lastMonth !== -1 || monthDay.getDate() <= 7) {
        months += `<text x="${labelW + w * pitch}" y="10">${monthDay.toLocaleString("en", { month: "short" })}</text>`;
      }
      lastMonth = monthDay.getMonth();
    }
    for (let r = 0; r < 7; r++) {
      const day = firstDay + w * 7 + r;
      if (day > 0) continue;
      const d = days.get(day);
      const tip = `<b>${d ? describeDay(d) : "No dictation"}</b><br>${dayFmt.format(dateOf(day))}`;
      cells += `<rect class="cell l${level(d?.[state.heatMetric])}" x="${labelW + w * pitch}" y="${labelH + r * pitch}" width="${cell}" height="${cell}" data-tip="${esc(tip)}"/>`;
    }
  }
  const rows = ["Mon", "", "Wed", "", "Fri", "", ""]
    .map((l, r) => l && `<text x="0" y="${labelH + r * pitch + cell - 2}">${l}</text>`).join("");
  const svgW = labelW + weeks * pitch - gap;
  const svgH = labelH + 7 * pitch - gap;
  host.innerHTML = `<svg width="${svgW}" height="${svgH}" role="img" aria-label="Dictation activity for the last ${weeks} weeks">${months}${rows}${cells}</svg>`;
}

function describeDay(d) {
  if (state.heatMetric === "words") return `${plural(d.words, "word")} from ${plural(d.count, "dictation")}`;
  if (state.heatMetric === "seconds") return `${duration(d.seconds)} of audio`;
  return `${plural(d.count, "dictation")}, ${plural(d.words, "word")}`;
}

// ---------------------------------------------------------------- when you talk

function hourCounts() {
  const counts = Array(24).fill(0);
  for (const s of inRange(sessions, rangeBounds(state.range))) counts[s.hour]++;
  return counts;
}

function persona(hour) {
  if (hour < 5) return "Night owl";
  if (hour < 12) return "Morning talker";
  if (hour < 17) return "Afternoon talker";
  if (hour < 21) return "Evening talker";
  return "Night owl";
}

function hoursCard() {
  const current = inRange(sessions, rangeBounds(state.range));
  if (current.length < 20) {
    return `<section class="card hours"><div class="card-head"><h3>When you talk</h3></div>
      <p class="empty">After a few days of dictation this shows the hours you talk most.</p></section>`;
  }
  const counts = hourCounts();
  const peak = counts.indexOf(Math.max(...counts));
  const weekdays = Array(7).fill(0);
  for (const s of current) weekdays[dateOf(s.day).getDay()]++;
  const busiest = weekdays.indexOf(Math.max(...weekdays));
  const busiestName = weekdayFmt.format(new Date(2026, 8, 20 + busiest)); // 20 Sep 2026 is a Sunday
  return `<section class="card hours">
    <div class="card-head"><h3>When you talk</h3></div>
    <p class="verdict"><b>${persona(peak)}.</b> <span class="muted">You dictate most around ${hourLabel(peak)}, and ${busiestName}s are your busiest day.</span></p>
    <div id="hours"></div>
  </section>`;
}

function drawHours() {
  const host = document.getElementById("hours");
  if (!host) return;
  const counts = hourCounts();
  const max = Math.max(...counts);
  const peak = counts.indexOf(max);
  const width = host.clientWidth, height = 96, axis = 16, gap = 2;
  const barW = (width - gap * 23) / 24;
  let bars = "";
  counts.forEach((c, h) => {
    const x = h * (barW + gap);
    const bh = c ? Math.max(3, (c / max) * (height - axis - 4)) : 0;
    const tip = `<b>${hourLabel(h)} to ${hourLabel((h + 1) % 24)}</b><br>${plural(c, "dictation")}`;
    bars += `<g data-tip="${esc(tip)}"><rect class="hit" x="${x}" y="0" width="${barW + gap}" height="${height - axis}"/>
      <path class="bar${h === peak ? " peak" : ""}" d="${roundedTop(x, height - axis - bh, barW, bh, Math.min(2, bh / 2))}"/></g>`;
  });
  const ticks = [0, 6, 12, 18].map((h) => `<text x="${h * (barW + gap)}" y="${height - 2}">${hourLabel(h)}</text>`).join("");
  host.innerHTML = `<svg width="${width}" height="${height}" role="img" aria-label="Dictations by hour of day">
    <line x1="0" x2="${width}" y1="${height - axis + 0.5}" y2="${height - axis + 0.5}"/>${bars}${ticks}</svg>`;
}

function roundedTop(x, y, w, h, r) {
  if (h <= 0) return "";
  return `M${x},${y + h}V${y + r}Q${x},${y} ${x + r},${y}H${x + w - r}Q${x + w},${y} ${x + w},${y + r}V${y + h}Z`;
}

// ---------------------------------------------------------------- pace

function paceCard() {
  const current = inRange(sessions, rangeBounds(state.range));
  const words = sum(current, "words");
  const seconds = sum(current, "seconds");
  if (!current.length) {
    return `<section class="card"><div class="card-head"><h3>Pace</h3></div><p class="empty">No dictation in this period.</p></section>`;
  }
  const wpm = Math.round(words / (seconds / 60));
  const saved = Math.max(0, (words / TYPING_WPM) * 60 - seconds);
  const scale = Math.max(wpm, 160);
  return `<section class="card">
    <div class="card-head"><h3>Pace</h3></div>
    <div class="split">
      <div><span class="big num">${wpm}<small>wpm</small></span><span class="muted small">Your speaking pace</span></div>
      <div><span class="big num">${duration(saved).replace(/ (min|h)/g, "<small>$1</small>")}</span><span class="muted small">Saved over typing</span></div>
    </div>
    <div class="hbar-list">
      <div class="hbar"><span class="name">You, speaking</span><div class="track"><div class="fill" style="width:${(wpm / scale) * 100}%"></div></div><span class="val">${wpm}</span></div>
      <div class="hbar"><span class="name">Typical typing</span><div class="track"><div class="fill muted" style="width:${(TYPING_WPM / scale) * 100}%"></div></div><span class="val">${TYPING_WPM}</span></div>
    </div>
    <p class="note muted small">That's ${(wpm / TYPING_WPM).toFixed(1)}× faster than typing at ${TYPING_WPM} words per minute.</p>
  </section>`;
}

// ---------------------------------------------------------------- apps

function appsCard() {
  const current = inRange(sessions, rangeBounds(state.range));
  if (!current.length) {
    return `<section class="card"><div class="card-head"><h3>Where your words go</h3></div><p class="empty">No dictation in this period.</p></section>`;
  }
  const totals = new Map();
  for (const s of current) totals.set(s.app, (totals.get(s.app) || 0) + s.words);
  const sorted = [...totals.entries()].sort((a, b) => b[1] - a[1]);
  const top = sorted.slice(0, 5);
  const rest = sorted.slice(5).reduce((n, [, w]) => n + w, 0);
  if (rest) top.push([`${sorted.length - 5} other apps`, rest]);
  const total = sum(current, "words");
  const max = top[0][1];
  return `<section class="card">
    <div class="card-head"><h3>Where your words go</h3></div>
    <div class="hbar-list">
      ${top.map(([name, w], i) => {
        const app = APPS.find((a) => a.name === name);
        return `<div class="hbar" data-tip="<b>${esc(name)}</b><br>${plural(w, "word")}">
          <span class="name">${esc(name)}${app ? `<small>${app.profile}</small>` : ""}</span>
          <div class="track"><div class="fill${i === 0 ? "" : " muted"}" style="width:${(w / max) * 100}%"></div></div>
          <span class="val">${Math.round((w / total) * 100)}%</span></div>`;
      }).join("")}
    </div>
  </section>`;
}

// ---------------------------------------------------------------- cleanup

function cleanupCard() {
  const current = inRange(sessions, rangeBounds(state.range));
  const fillers = sum(current, "fillers");
  if (!fillers) {
    return `<section class="card"><div class="card-head"><h3>Cleanup</h3></div><p class="empty">Refinement hasn't removed anything yet.</p></section>`;
  }
  const weight = FILLERS.reduce((n, [, w]) => n + w, 0);
  const rows = FILLERS.slice(0, 5).map(([word, w]) => [word, Math.round((fillers * w) / weight)]);
  const max = rows[0][1];
  const corrections = state.scenario === "new" ? 0 : Math.min(18, Math.round(current.length / 60));
  return `<section class="card">
    <div class="card-head"><h3>Cleanup</h3></div>
    <div class="split">
      <div><span class="big num">${fmt(fillers)}</span><span class="muted small">Filler words removed</span></div>
      <div><span class="big num">${corrections}</span><span class="muted small">Corrections learned</span></div>
    </div>
    <div class="hbar-list">
      ${rows.map(([word, n], i) => `<div class="hbar"><span class="name">"${word}"</span>
        <div class="track"><div class="fill${i === 0 ? "" : " muted"}" style="width:${(n / max) * 100}%"></div></div>
        <span class="val">${fmt(n)}</span></div>`).join("")}
    </div>
  </section>`;
}

// ---------------------------------------------------------------- records

function recordsCard(days, streak) {
  const first = sessions[0];
  const longest = sessions.reduce((a, b) => (b.seconds > a.seconds ? b : a));
  let busiest = [0, { count: 0 }], wordiest = [0, { words: 0 }];
  for (const entry of days) {
    if (entry[1].count > busiest[1].count) busiest = entry;
    if (entry[1].words > wordiest[1].words) wordiest = entry;
  }
  const total = sum(sessions, "words");
  const next = MILESTONES.find((m) => m > total);
  const reached = MILESTONES.filter((m) => m <= total).pop();
  const row = (title, sub, value) => `<div class="r"><div class="t"><span>${title}</span>${sub ? `<small>${sub}</small>` : ""}</div><span class="v">${value}</span></div>`;
  return `<section class="card rows">
    ${row(`Next milestone: ${fmt(next)} words`, `${fmt(next - total)} to go${reached ? `. You passed ${fmt(reached)} already.` : ""}`,
      `<progress value="${total}" max="${next}"></progress>`)}
    ${row("Longest streak", streak.bestEnd === 0 && streak.current ? "That's the one you're on" : `Ended ${relativeDay(streak.bestEnd)}`, plural(streak.best, "day"))}
    ${row("Longest dictation", `${longest.words} words into ${esc(longest.app)}, ${relativeDay(longest.day)}`, clock(longest.seconds))}
    ${row("Busiest day", relativeDay(busiest[0]).replace(/^./, (c) => c.toUpperCase()), plural(busiest[1].count, "dictation"))}
    ${row("Wordiest day", relativeDay(wordiest[0]).replace(/^./, (c) => c.toUpperCase()), plural(wordiest[1].words, "word"))}
    ${row("First dictation", dateFmt.format(dateOf(first.day)), first.day === 0 ? "Today" : `${plural(-first.day, "day")} ago`)}
  </section>`;
}

// ---------------------------------------------------------------- insights off / empty

function insightsUnavailableCard() {
  if (!state.insights) {
    return `<section class="card notice-card">
      <div class="t"><h3>Insights are off</h3>
        <p class="muted">Speecher isn't keeping any record of your dictation. If you turn insights on,
          your stats are stored only on this computer and never sent to the cloud.</p></div>
      <button class="button" data-action="open-general">Insights settings…</button>
    </section>`;
  }
  return `<section class="card notice-card">
    <div class="t"><h3>No insights yet</h3>
      <p class="muted">Your stats appear here after your next dictation. They're stored only on this
        computer and never sent to the cloud.</p></div>
  </section>`;
}

// ---------------------------------------------------------------- General page

function generalPage() {
  const check = (label, checked, bind = "") =>
    `<div class="r"><div class="t"><span>${label}</span></div>
      <input type="checkbox" ${checked ? "checked" : ""} ${bind ? `data-bind-check="${bind}"` : ""} aria-label="${label}"></div>`;
  const clearRow = state.confirmClear
    ? `<div class="r confirm"><div class="t"><span>Delete all insights history?</span>
         <small>Your stats, streaks and records are erased. This can't be undone.</small></div>
         <button class="button" data-action="clear-cancel">Cancel</button>
         <button class="button destructive" data-action="clear-confirm">Delete History</button></div>`
    : `<button class="r row-button" data-action="clear-ask" ${sessions.length ? "" : "disabled"}>
         <div class="t"><span>Clear insights history…</span>
         <small>${sessions.length ? `${plural(sessions.length, "dictation")} recorded since ${dateFmt.format(dateOf(sessions[0].day))}.` : "Nothing is recorded."}</small></div>
         ${icon(ICONS.chevron)}</button>`;
  return `<div class="form-column">
    <h2 class="section-title">Dictation</h2>
    <section class="card rows">
      ${check("Pause playing media while dictating", true)}
      ${check("Play sounds when dictation starts and stops", false)}
      ${check("Show live text in the popup while you speak", true)}
    </section>
    <h2 class="section-title">Insights</h2>
    <section class="card rows">
      <div class="r"><div class="t"><span>Keep insights about your dictation</span>
        <small>Records word counts, times and app names for the stats on Home. Never the text or audio.
          Stored only on this computer and never sent to the cloud.</small></div>
        <input type="checkbox" ${state.insights ? "checked" : ""} data-bind-check="insights" aria-label="Keep insights about your dictation"></div>
      ${state.insights ? "" : `<div class="r note-row"><div class="t"><small>Nothing new is recorded while this is off. History you already have stays until you clear it.</small></div></div>`}
      ${clearRow}
    </section>
    <h2 class="section-title">Setup</h2>
    <section class="card rows">
      <button class="r row-button"><div class="t"><span>Run setup assistant…</span>
        <small>Go through the first-run steps again, from sign-in to the Global Shortcut.</small></div>${icon(ICONS.chevron)}</button>
    </section>
  </div>`;
}

// ------------------------------------------------------------------ wiring

function applyChrome() {
  document.body.dataset.theme = state.theme;
  document.body.dataset.accent = state.accent;
  document.body.dataset.width = state.width;
  if (params.has("full")) document.body.dataset.full = "";
  for (const select of document.querySelectorAll(".mock-bar select")) {
    select.value = state[select.dataset.param === "state" ? "scenario" : select.dataset.param];
  }
}

function syncUrl() {
  const url = new URL(location.href);
  url.searchParams.set("state", state.scenario);
  url.searchParams.set("theme", state.theme);
  url.searchParams.set("accent", state.accent);
  url.searchParams.set("width", state.width);
  url.searchParams.set("page", state.page);
  url.searchParams.set("insights", state.insights ? "on" : "off");
  history.replaceState(null, "", url);
}

document.querySelector(".mock-bar").addEventListener("change", (event) => {
  const key = event.target.dataset.param;
  if (key === "state") { state.scenario = event.target.value; sessions = generate(state.scenario); }
  else state[key] = event.target.value;
  applyChrome(); syncUrl(); render();
});

document.querySelector(".sidebar ul").addEventListener("click", (event) => {
  const page = event.target.closest("li")?.dataset.page;
  if (!page) return;
  state.page = page; state.confirmClear = false;
  syncUrl(); render();
});

document.getElementById("column").addEventListener("change", (event) => {
  if (event.target.dataset.bindCheck === "insights") {
    state.insights = event.target.checked;
    syncUrl(); render();
    return;
  }
  const key = event.target.dataset.bind;
  if (!key) return;
  state[key] = event.target.value;
  render();
});

document.getElementById("column").addEventListener("click", (event) => {
  const action = event.target.closest("[data-action]")?.dataset.action;
  if (action === "open-general") {
    event.preventDefault();
    state.page = "general"; syncUrl(); render();
  } else if (action === "clear-ask" || action === "clear-cancel") {
    state.confirmClear = action === "clear-ask"; render();
  } else if (action === "clear-confirm") {
    sessions = []; state.confirmClear = false; render();
  } else if (action === "toggle") {
    if (state.dictation === "listening" && state.insights) {
      // Finishing a dictation adds a session, so the page updates like the real one would.
      const s = makeSession(rng(Date.now()), 0);
      s.hour = 15; s.app = "Slack";
      sessions.push(s);
    }
    state.dictation = state.dictation === "idle" ? "listening" : "idle";
    render();
  } else if (action === "copy") {
    const button = event.target.closest("button");
    button.innerHTML = icon(ICONS.check);
    button.dataset.tip = "Copied";
    showTip(button);
    setTimeout(() => { button.innerHTML = icon(ICONS.copy); button.dataset.tip = "Copy transcript"; }, 1500);
  }
});

const tooltip = document.getElementById("tooltip");
function showTip(target) {
  tooltip.innerHTML = target.dataset.tip;
  tooltip.hidden = false;
  const box = target.getBoundingClientRect();
  const tip = tooltip.getBoundingClientRect();
  const left = Math.min(Math.max(4, box.left + box.width / 2 - tip.width / 2), innerWidth - tip.width - 4);
  const top = box.top - tip.height - 6 < 4 ? box.bottom + 6 : box.top - tip.height - 6;
  tooltip.style.left = `${left}px`;
  tooltip.style.top = `${top}px`;
}
document.addEventListener("mouseover", (event) => {
  const target = event.target.closest("[data-tip]");
  if (target) showTip(target); else tooltip.hidden = true;
});

let frame = 0;
new ResizeObserver(() => {
  cancelAnimationFrame(frame);
  frame = requestAnimationFrame(() => {
    if (document.getElementById("heatmap")) { drawHeatmap(byDay(sessions)); drawHours(); }
  });
}).observe(document.getElementById("content"));

applyChrome();
render();
