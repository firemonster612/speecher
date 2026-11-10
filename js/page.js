// The page's scenes. Two scroll-driven moments, the tagline's two halves: the
// hero's night becoming the pill ("Talk."), and the pinned delivery stage,
// where the words land in the window you were in and the layout opens onto
// the password field that refuses them ("It lands."). Everything else scrolls
// normally, with the light and the demos moving on their own clocks, and the
// clicks and hovers that show the real product.
(() => {
  const { Voice, Talker, Range, HERO, pillLook, lerpLook, hex } = window.Ridge;
  const root = document.documentElement;
  const RM = root.classList.contains('rm');
  // A change of motion preference while the page is open: reload into the
  // other mode, which commits its whole layout cleanly.
  matchMedia('(prefers-reduced-motion: reduce)').addEventListener('change', () => location.reload());
  const $ = (s, el = document) => el.querySelector(s);
  const $$ = (s, el = document) => [...el.querySelectorAll(s)];
  const clamp = (x, a = 0, b = 1) => Math.min(b, Math.max(a, x));
  const lerp = (a, b, t) => a + (b - a) * t;
  const span = (x, a, b) => clamp((x - a) / (b - a));
  const bezier = (x1, y1, x2, y2) => (x) => {
    if (x <= 0) return 0; if (x >= 1) return 1;
    let t = x;
    for (let i = 0; i < 8; i++) {
      const cx = 3 * x1 * t * (1 - t) ** 2 + 3 * x2 * t * t * (1 - t) + t ** 3 - x;
      const dx = 3 * x1 * (1 - t) ** 2 + 6 * (x2 - x1) * t * (1 - t) + 3 * (1 - x2) * t * t;
      if (Math.abs(cx) < 1e-5 || !dx) break;
      t = clamp(t - cx / dx);
    }
    return 3 * y1 * t * (1 - t) ** 2 + 3 * y2 * t * t * (1 - t) + t ** 3;
  };
  // The plan's easing tokens.
  const easeOut = bezier(.165, .84, .44, 1), easeInOut = bezier(.645, .045, .355, 1);
  const phone = () => innerWidth <= 860;
  // Backing resolution: the hero and small views up to 1.25x; the big
  // secondary views at 1x, to keep frames cheap.
  const density = (cap = 1.25) => Math.min(cap, devicePixelRatio || 1);
  // A light's colours from one accent: a pale core, the accent far, its halo.
  const tint = (accent, rgb = hex(accent), white = .82) => {
    const a = rgb;
    return { core: a.map((v) => v + (1 - v) * white), far: a.map((v) => v * .7), halo: a };
  };
  // The instrument's horizon keeps its colour where its lines crowd together.
  const HORIZON_WHITE = .4;

  // Someone talking into a range: a talker, a voice, and whatever draws it.
  // With reduced motion the voice is a still level.
  function actor(seed, level, mode = 'quiet') {
    const talker = new Talker(seed), voice = new Voice();
    talker.set(mode);
    const a = { talker, voice, level, step(dt) { if (RM) return; talker.update(dt); voice.update(dt, talker.loudness); } };
    if (RM) { voice.reduced = true; voice.hist.fill(level); }
    return a;
  }
  const actors = [];
  const visible = new Map();
  const watch = new IntersectionObserver((entries) => {
    for (const e of entries) visible.set(e.target, e.isIntersecting);
  }, { rootMargin: '120px 0px' });
  const seen = (el) => visible.get(el) ?? false;
  const pills = [];
  function makePill(mount, voice, opts) { const p = new Pill(mount, voice, opts); pills.push(p); watch.observe(mount); return p; }
  // A roving radio group: one tab stop, arrows move and select.
  function radios(buttons, onSelect) {
    const select = (b, focus) => {
      for (const o of buttons) { const on = o === b; o.setAttribute('aria-checked', String(on)); o.tabIndex = on ? 0 : -1; }
      if (focus) { b.focus(); b.scrollIntoView({ block: 'nearest', inline: 'nearest' }); }
      onSelect(b);
    };
    buttons.forEach((b, i) => {
      b.addEventListener('click', () => select(b));
      b.addEventListener('keydown', (e) => {
        const d = e.key === 'ArrowRight' || e.key === 'ArrowDown' ? 1 : e.key === 'ArrowLeft' || e.key === 'ArrowUp' ? -1 : 0;
        if (!d) return;
        e.preventDefault();
        select(buttons[(i + d + buttons.length) % buttons.length], true);
      });
    });
    return select;
  }

  // 1. Hero ---------------------------------------------------------------
  const intro = $('.intro'), stage = $('.intro-stage'), heroLayer = $('.hero'), night = $('#night');
  const heroCanvas = $('#hero-canvas'), heroCopy = $('.hero-copy');
  const heroActor = actor(11, .62, 'phrase'); heroActor.talker.loopGap = 800; actors.push(heroActor);
  // Pre-roll a second of the phrase, so the first frame already has a lit range.
  if (!RM) for (let i = 0; i < 70; i++) heroActor.step(1000 / 60);
  const hero = new Range(heroCanvas, heroActor.voice, { ...HERO });
  watch.observe(intro);
  // Speecher's default dictation shortcut on this computer
  // (GlobalShortcutBinder.h): Meta+Alt+D, and Ctrl+Alt+D on Windows. On
  // macOS Qt's Meta is the Control key (MacGlobalShortcutBinder.mm). The app
  // draws the shortcut from Qt's native text split at "+"
  // (ShortcutBinding::displayText, QuickSettings.cpp shortcutKeys); macOS's
  // native text has no "+", so a Mac shows one cap, ⌃⌥D. Phones and
  // ChromeOS have no Speecher shortcut, so they get no key names.
  const ua = navigator.userAgent, plat = navigator.userAgentData?.platform || navigator.platform || '';
  const KEYS = /Android|iPhone|iPad|iPod|CrOS/i.test(ua) || (/Mac/.test(plat) && navigator.maxTouchPoints > 1) ? null
    : /Win/i.test(plat) ? { caps: ['Ctrl', 'Alt', 'D'], held: (e) => e.ctrlKey && e.altKey }
    : /Mac/i.test(plat) ? { caps: ['\u2303\u2325D'], name: 'Control Option D', held: (e) => e.ctrlKey && e.altKey }
    : /Linux|X11/i.test(plat + ua) ? { caps: ['Meta', 'Alt', 'D'], held: (e) => e.metaKey && e.altKey } : null;
  const lean = { x: 0, y: 0, tx: 0, ty: 0 };
  if (!RM && matchMedia('(hover: hover)').matches) {
    addEventListener('pointermove', (e) => { lean.tx = e.clientX / innerWidth * 2 - 1; lean.ty = e.clientY / innerHeight * 2 - 1; });
  }
  const born = performance.now();
  function heroBox(W, H) {
    if (phone()) { const w = W * .94; return { x: (W - w) / 2, cy: H * .3, w, h: Math.min(250, w * .68) }; }
    const w = Math.min(1240, W * .9), h = Math.min(420, H * .48);
    return { x: (W - w) / 2 + lean.x * 22, cy: H * .34, w, h };
  }

  // 2. One instrument. The hand-off lands the pill here; it hears one spoken
  //    sentence, and only when it has finished does the result write itself
  //    into the app's composer, marked as the app marks an edit (struck in
  //    red, added in blue), then settled. Pick an app and the same words land
  //    written for it; pick a level and its profile cleans up more or less.
  //    Level, tone and output language belong to the writing profile, not to
  //    the app (Target.h), so apps in one profile share them: Slack and
  //    Notion are both Work (Apps.cpp). Discord sits in a custom profile that
  //    writes German. At None nothing refines and the words land as heard,
  //    unless the profile translates, which then cleans up lightly (Target.cpp).
  const SPOKEN = "um so the upload test no wait the login test is failing again can you take a look don't change anything yet thanks";
  const said = SPOKEN.split(' ');
  const LEVEL_NAMES = { none: 'None', light: 'Light', medium: 'Medium', high: 'High' };
  // Sample settings. The colours are the page's profile accents.
  const PROFILES = {
    other: { name: 'Other', colour: '#7894E4', tone: 'No tone', level: 'medium' },
    email: { name: 'Email', colour: '#F2A65E', tone: 'Formal', level: 'high' },
    work: { name: 'Work', colour: '#4FD1C5', tone: 'No tone', level: 'light' },
    ai: { name: 'AI coding', colour: '#B79CFF', tone: 'No tone', level: 'medium' },
    personal: { name: 'Personal', colour: '#FF8AB8', tone: 'Very casual', level: 'medium' },
    german: { name: 'German', colour: '#E8C547', tone: 'Casual', level: 'medium', lang: 'de', custom: true },
  };
  // What each level writes (\n\n a paragraph, `x` code). None is the words as heard unless given.
  const PLAIN = {
    light: "Um, so the login test is failing again. Can you take a look? Don't change anything yet. Thanks.",
    medium: "The login test is failing again. Can you take a look? Don't change anything yet. Thanks.",
    high: 'The login test is failing again. Could you take a look without changing anything yet? Thanks.',
  };
  // An agent's prompt names what the window shows, and keeps the request and its limit.
  const coding = (name) => ({
    light: PLAIN.light,
    medium: `\`${name}\` is failing again. Can you take a look? Don't change anything yet. Thanks.`,
    high: `\`${name}\` is failing again. Please take a look, but don't change anything yet.`,
  });
  const GERMAN_LIGHT = 'Ähm, also der Login-Test schlägt schon wieder fehl. Kannst du mal draufschauen? Ändere noch nichts, danke.';
  const APPS = {
    anywhere: { name: 'Anywhere', profile: 'other', out: PLAIN },
    mail: { name: 'Mail', profile: 'email', out: {
      light: "Um, so the login test is failing again. Can you take a look? Don't change anything yet. Thank you.",
      medium: "The login test is failing again. Could you take a look? Please don't change anything yet. Thank you.",
      high: "The login test is failing again. Could you please take a look?\n\nPlease don't make any changes for now. Thank you." } },
    slack: { name: 'Slack', profile: 'work', out: PLAIN },
    notion: { name: 'Notion', profile: 'work', out: PLAIN },
    claude: { name: 'Claude Code', profile: 'ai', out: coding('tests/login_test.py') },
    cursor: { name: 'Cursor', profile: 'ai', out: coding('test_login_redirect') },
    whatsapp: { name: 'WhatsApp', profile: 'personal', out: {
      light: "Um, so the login test is failing again, can you take a look? Don't change anything yet, thanks!",
      medium: "login test's failing again \u{1F629} can u take a look? don't touch anything yet tho, thx!",
      high: "login test's failing again \u{1F629} mind taking a look? don't touch anything yet tho, thx!" } },
    discord: { name: 'Discord', profile: 'german', out: {
      none: GERMAN_LIGHT, light: GERMAN_LIGHT,
      medium: 'Der Login-Test schlägt schon wieder fehl, kannst du mal draufschauen? Bitte noch nichts ändern, danke!',
      high: 'Der Login-Test schlägt schon wieder fehl. Kannst du ihn dir ansehen, ohne schon etwas zu ändern? Danke!' } },
  };
  const written = (app, lvl) => APPS[app].out[lvl] ?? SPOKEN;
  const esc = (t) => t.replace(/[&<>"]/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;' })[c]);
  const code = (t) => esc(t).replace(/`([^`]+)`/g, '<code>$1</code>');

  // The edit, word by word against what was said, as the app's DiffText
  // shows it: punctuation is its own token, so "again." keeps "again" and
  // adds the full stop. In each changed run, punctuation that closes the
  // word before comes first, then the struck words, then the added ones.
  const tokens = (text) => text.replace(/\n\n/g, ' ').split(' ').flatMap((w) => {
    const m = w.match(/^(.*?[^.,?!])([.,?!]+)$/);
    return m ? [m[1], m[2]] : [w];
  });
  const glued = (t) => /^[.,?!]+$/.test(t);
  function diff(a, b) {
    const n = a.length, m = b.length, L = Array.from({ length: n + 1 }, () => new Array(m + 1).fill(0));
    for (let i = n - 1; i >= 0; i--) for (let j = m - 1; j >= 0; j--) L[i][j] = a[i] === b[j] ? L[i + 1][j + 1] + 1 : Math.max(L[i + 1][j], L[i][j + 1]);
    const runs = [];
    const push = (kind, t) => { const r = runs.at(-1); if (r?.kind === kind) r.words.push(t); else runs.push({ kind, words: [t] }); };
    let i = 0, j = 0, del = [], ins = [];
    const flush = () => {
      const lead = del.length && glued(ins[0] ?? '') && runs.at(-1)?.kind === 'same' ? [ins.shift()] : [];
      lead.forEach((t) => push('ins', t)); del.forEach((t) => push('del', t)); ins.forEach((t) => push('ins', t)); del = []; ins = [];
    };
    while (i < n || j < m) {
      if (i < n && j < m && a[i] === b[j]) { flush(); push('same', a[i]); i++; j++; }
      else if (j >= m || (i < n && L[i + 1][j] >= L[i][j + 1])) del.push(a[i++]);
      else ins.push(b[j++]);
    }
    flush();
    return runs;
  }
  function marksHtml(text, lang) {
    const join = (words) => words.reduce((s, t, k) => s + (k && !glued(t) ? ' ' : '') + code(t), '');
    return diff(said, tokens(text)).map((r, k) => {
      const body = join(r.words), gap = k && !glued(r.words[0]) ? ' ' : '';
      return gap + (r.kind === 'same' ? body : `<${r.kind}${r.kind === 'ins' && lang ? ` lang="${lang}"` : ''}>${body}</${r.kind}>`);
    }).join('');
  }
  const cleanHtml = (text) => text.split('\n\n').map((p) => `<p>${code(p)}</p>`).join('').replace(/<\/p>$/, '<i class="caret"></i></p>');

  // Each app as a composer strip: its own header, composer and type. The
  // strip is as tall as the longest edit needs; what is left over shows the
  // app's own context (the last message, the failing test), which the
  // composer grows over as a real one does. The sentence is most of it.
  const SAY = (ph) => `<div class="say" data-show="empty"><p class="say-ph">${ph}<i class="caret"></i></p><div class="say-marks"></div><div class="say-clean"></div></div>`;
  const HIST = (html) => `<div class="hist">${html}</div>`;
  const STRIPS = {
    anywhere: () => `<div class="bar"><span class="any-icons"><span class="ico" style="--i: url(icons/gmail.svg)"></span><span class="ico" style="--i: url(icons/googledocs.svg)"></span><span class="ico" style="--i: url(icons/googlemessages.svg)"></span><span class="ico" style="--i: url(icons/firefoxbrowser.svg)"></span></span><b>Any other app</b></div>
      <div class="comp">${SAY('')}</div>`,
    mail: () => `<div class="bar"><span class="lights"><i></i><i></i><i></i></span><b>Re: Login test</b><span class="to"><span>To:</span> Sam Okafor</span><span class="send">&#x2191;</span></div>
      <div class="comp">${SAY('')}</div>${HIST('<p class="quote"><b>On Monday, Sam Okafor wrote:</b> I pushed the session changes this morning. Let me know if anything breaks.</p>')}`,
    slack: () => `<div class="bar"><span class="ico ico-own" style="--i: url(icons/slack.svg)"></span><b># backend</b><span class="bar-r">Upload team</span></div>
      ${HIST('<div class="m"><span class="av j">J</span><p><b>Jun</b><small>1:52 PM</small><br>merged the session store change</p></div><div class="m"><span class="av">R</span><p><b>Rosa</b><small>2:14 PM</small><br>deploy is out, ping me if anything looks off</p></div>')}
      <div class="comp"><div class="box">${SAY('Message #backend')}<div class="tools"><i>B</i><i><em>I</em></i><i><s>S</s></i><span class="send"><svg viewBox="0 0 24 24"><path d="M3 20V4l19 8z"/></svg></span></div></div></div>`,
    notion: () => `<div class="bar"><span class="ico" style="--i: url(icons/notion.svg); --c: #37352F"></span><span class="crumbs">Engineering <i>/</i> <b>Release checklist</b></span></div>
      ${HIST('<p class="todo"><i></i>Ship the upload server</p><p class="todo"><i></i>Login test green</p>')}
      <div class="comp"><span class="av">A</span>${SAY('Add a comment…')}<span class="send">&#x2191;</span></div>`,
    claude: () => `<div class="bar"><b>&#x273B; Claude Code</b><span class="bar-r">~/upload-service</span></div>
      ${HIST('<p class="you">&gt; run the tests</p><p class="dimt">&#x23FA; Bash(pytest tests/)</p><p class="err"><span>&#x23BF;</span> tests/login_test.py failed, 1 of 48</p>')}
      <div class="comp"><div class="box"><span class="ps">&gt;</span>${SAY('')}</div></div>`,
    cursor: () => `<div class="bar"><span class="tab">login_test.py</span><code class="line"><i>12</i><span class="k">def</span> <span class="f">test_login_redirect</span>(client):</code></div>
      ${HIST('<p class="ask">run the tests</p><p class="turn"><b>Tests</b> <span class="ok">47 passed</span>, <span class="bad">1 failed</span>: test_login_redirect</p>')}
      <div class="comp"><div class="box"><span class="ctx">@ login_test.py</span>${SAY('Plan, search, build anything')}</div></div>`,
    whatsapp: () => `<div class="bar"><span class="av">R</span><span class="who"><b>Rosa</b><small>online</small></span></div>
      ${HIST('<p class="b out">pushing the session fix now<small>14:02</small></p><p class="b">did the fix go in?<small>14:14</small></p>')}
      <div class="comp"><div class="box">${SAY('Type a message')}</div><span class="mic"><svg viewBox="0 0 24 24"><path d="M12 14a3 3 0 0 0 3-3V5a3 3 0 0 0-6 0v6a3 3 0 0 0 3 3zm5-3a5 5 0 0 1-10 0H5a7 7 0 0 0 6 6.92V21h2v-3.08A7 7 0 0 0 19 11z"/></svg></span></div>`,
    discord: () => `<div class="bar"><b># dev</b><span class="bar-r">Upload-Team</span></div>
      ${HIST('<div class="m"><span class="av b"></span><p><b class="n2">Mara</b><small>14:02</small><br><span lang="de">Build ist grün</span></p></div><div class="m"><span class="av"></span><p><b>Jonas</b><small>14:14</small><br><span lang="de">ist der Fix schon drin?</span></p></div>')}
      <div class="comp"><div class="box"><span class="plus">+</span>${SAY('Nachricht an #dev')}</div></div>`,
  };
  const DARK = new Set(['claude', 'cursor', 'whatsapp', 'discord']);
  const says = $('#says'), saysPillMount = $('.says-pill'), strip = $('#strip-app'), panel = $('#strip');
  const tabs = $$('#apps [role="tab"]'), levelButtons = $$('.levels [role="radio"]');
  const live = $('#says-live');
  const levels = Object.fromEntries(Object.entries(PROFILES).map(([id, p]) => [id, p.level]));
  const ins = { app: 'anywhere', say: null, shown: '', marksUntil: 0, pass: null, delivered: false, user: false, started: false };
  const saysPill = makePill(saysPillMount, heroActor.voice);
  function buildStrip(app) {
    strip.className = `strip s-${app}${DARK.has(app) ? ' dark' : ''}${strip.classList.contains('measuring') ? ' measuring' : ''}`;
    strip.innerHTML = STRIPS[app]();
    const lang = PROFILES[APPS[app].profile].lang;
    ins.say = $('.say', strip);
    // A translated result is marked with its language; the struck words as heard stay English.
    if (lang) for (const el of [$('.say-ph', ins.say), $('.say-clean', ins.say)]) el.lang = lang;
  }
  // What the composer shows: its placeholder, the marked edit, or the clean text.
  function show(what, dim = false) {
    const { app } = ins, lvl = levels[APPS[app].profile], text = written(app, lvl);
    const lang = PROFILES[APPS[app].profile].lang;
    if (what !== 'empty') {
      const key = `${app}|${lvl}`;
      if (ins.shown !== key) {
        $('.say-marks', ins.say).innerHTML = text === SPOKEN ? `<p>${esc(text)}</p>` : `<p>${marksHtml(text, lang)}</p>`;
        $('.say-clean', ins.say).innerHTML = cleanHtml(text);
        ins.shown = key;
      }
    } else ins.shown = '';
    ins.say.dataset.show = what;
    ins.say.classList.toggle('dim', dim);
    fitHistory();
  }
  // A message the composer has grown over goes, whole, rather than clipped.
  function fitHistory() {
    for (const h of $$('.hist', strip)) for (const c of h.children) c.classList.toggle('gone', c.offsetTop < 0 || c.offsetTop + c.offsetHeight > h.clientHeight + 1);
  }
  // The profile's colour, chips and level control for the app in front.
  function showProfile() {
    const P = PROFILES[APPS[ins.app].profile];
    says.style.setProperty('--p', P.colour);
    $('#profile-name').textContent = `${P.custom ? 'Your ' : ''}${P.name} profile`;
    $('#profile-tone').textContent = P.tone;
    $('#profile-lang').hidden = !P.lang;
    const lvl = levels[APPS[ins.app].profile];
    for (const b of levelButtons) { const on = b.dataset.level === lvl; b.setAttribute('aria-checked', String(on)); b.tabIndex = on ? 0 : -1; }
    glowTarget = hex(P.colour);
    // Reduced motion has no frames: recolour the still light now.
    if (RM) { saysGlow.colours = tint(P.colour, undefined, HORIZON_WHITE); if (ins.started) draw(0, true); }
  }
  function describe() {
    const A = APPS[ins.app], P = PROFILES[A.profile], lvl = levels[A.profile];
    const text = written(ins.app, lvl).replace(/`/g, '');
    return `${A.name}, ${P.name} profile, ${P.tone.toLowerCase()}, ${LEVEL_NAMES[lvl]} cleanup${P.lang ? ', writes in German' : ''}: <span${P.lang ? ` lang="${P.lang}"` : ''}>${esc(text)}</span>`;
  }
  // The result lands: marked for a moment, then clean. Words as heard have no marks.
  const MARKS = 1200;
  function deliver(now) {
    ins.delivered = true;
    // Reduced motion keeps the marks: the edit, still.
    const raw = !refines();
    show(raw ? 'clean' : 'marks');
    ins.marksUntil = RM || raw ? 0 : now + MARKS;
    $('#strip-sr').innerHTML = describe();
    if (ins.user) live.innerHTML = describe();
  }
  const refines = () => written(ins.app, levels[APPS[ins.app].profile]) !== SPOKEN;
  // A change after the result has landed: the pill refines briefly (not at
  // None, which doesn't refine), then the new result lands.
  const REDO = 520;
  function redo(now, appChanged) {
    if (!ins.delivered) { if (appChanged) show('empty'); return; }
    if (RM) return deliver(now);
    // The old result stays, dimmed, until the new one is sent; a new app starts empty.
    ins.marksUntil = 0;
    if (appChanged) show('empty'); else ins.say.classList.add('dim');
    ins.pass = { kind: 'redo', start: now, refine: refines() ? REDO : 0 };
  }
  function selectApp(app, now, user) {
    ins.app = app; ins.user = user;
    for (const t of tabs) { const on = t.dataset.app === app; t.setAttribute('aria-selected', String(on)); t.tabIndex = on ? 0 : -1; }
    panel.setAttribute('aria-labelledby', `tab-${app}`);
    buildStrip(app); ins.shown = '';
    showProfile();
    redo(now, true);
    if (!RM) strip.animate([{ opacity: .6, transform: 'translateY(6px)' }, { opacity: 1, transform: 'none' }], { duration: 320, easing: 'cubic-bezier(.165, .84, .44, 1)' });
  }
  // Tabs: one tab stop, arrows, Home and End move and select.
  tabs.forEach((t, i) => {
    t.addEventListener('click', () => { stopAuto(); selectApp(t.dataset.app, lastNow, true); });
    t.addEventListener('keydown', (e) => {
      const k = { ArrowRight: i + 1, ArrowDown: i + 1, ArrowLeft: i - 1, ArrowUp: i - 1, Home: 0, End: tabs.length - 1 }[e.key];
      if (k === undefined) return;
      e.preventDefault();
      const next = tabs[(k + tabs.length) % tabs.length];
      next.focus();
      selectApp(next.dataset.app, lastNow, true);
    });
  });
  radios(levelButtons, (b) => { stopAuto(); levels[APPS[ins.app].profile] = b.dataset.level; ins.user = true; redo(lastNow, false); });
  // Idle auto-advance: 3 s after the result settles, the next app every 4 s,
  // with a thin line filling under the tab in front. It stops for good at the
  // first click or key in the instrument, and waits while a pointer is over it.
  const AUTO = 4000, IDLE = 3000;
  const auto = { stopped: false, hover: false, at: Infinity, from: 0 };
  const appsRow = $('#apps');
  // The page's clock at the last frame.
  let lastNow = 0;
  function stopAuto() { auto.stopped = true; auto.at = Infinity; appsRow.removeAttribute('data-auto'); }
  says.addEventListener('pointerdown', stopAuto);
  says.addEventListener('keydown', (e) => { if (e.key !== 'Tab' && e.key !== 'Shift') stopAuto(); });
  // Focus on a control means someone is using it: the tabs stay put.
  for (const el of [appsRow, $('.levels')]) el.addEventListener('focusin', stopAuto);
  says.addEventListener('pointerenter', () => { auto.hover = true; });
  says.addEventListener('pointerleave', () => { auto.hover = false; });
  function schedule(now, wait) { if (!auto.stopped && !RM) { auto.from = now; auto.at = now + wait; } }
  // The first pass, once the pill has landed: it hears the sentence, refines
  // it (still showing the last words, as the app does), then sends it.
  const HEAR_MS = 1600, REFINE_MS = 900;
  function instrumentAt(now) {
    const p = ins.pass;
    // While the pill hears the first pass, the app sits a little back under
    // the night: the pill is the bright thing, and the caret waits in it.
    says.toggleAttribute('data-hearing', p?.kind === 'first');
    if (!p) return;
    const t = now - p.start;
    if (p.kind === 'first') {
      const refine = refines() ? REFINE_MS : 0;
      if (t < HEAR_MS) saysPill.set({ phase: 'listening', words: said, count: Math.max(1, Math.ceil(t / HEAR_MS * said.length)), seconds: Math.floor(t / 1000) });
      else if (t < HEAR_MS + refine) saysPill.set({ phase: 'refining', words: said });
      else { saysPill.set(SENT); ins.pass = null; deliver(now); schedule(now, MARKS + IDLE); }
    } else {
      if (t < p.refine) saysPill.set({ phase: 'refining', words: [] });
      else { saysPill.set(SENT); ins.pass = null; deliver(now); }
    }
  }
  // Marks to clean: the composer keeps the marked text's height until the
  // crossfade has ended, so the two wraps never overlap a shrinking box.
  const FADE = 320;
  function instrumentTick(now, dt) {
    if (ins.marksUntil && now >= ins.marksUntil) {
      ins.marksUntil = 0;
      ins.say.style.minHeight = `${ins.say.offsetHeight}px`;
      show('clean');
      ins.holdUntil = now + FADE;
    }
    if (ins.holdUntil && now >= ins.holdUntil) { ins.holdUntil = 0; ins.say.style.minHeight = ''; fitHistory(); }
    const running = !auto.stopped && auto.at < Infinity;
    appsRow.toggleAttribute('data-auto', running);
    if (!running) return;
    if (auto.hover || !seen(says)) { auto.at += dt; auto.from += dt; }
    if (now >= auto.at) {
      const i = tabs.findIndex((t) => t.dataset.app === ins.app);
      selectApp(tabs[(i + 1) % tabs.length].dataset.app, now, false);
      schedule(now, AUTO);
    }
    appsRow.style.setProperty('--prog', clamp((now - auto.from) / (auto.at - auto.from)).toFixed(3));
  }
  // Landing and leaving: the instrument starts empty each time the pill lands.
  function land(now) { ins.delivered = false; show('empty'); ins.pass = { kind: 'first', start: now - 300 }; }
  function unland() { says.removeAttribute('data-hearing'); ins.pass = null; ins.delivered = false; ins.marksUntil = 0; auto.at = Infinity; show('empty'); }
  // Every result at once, to size the strip for the longest one, so nothing
  // under it moves as the apps and levels change.
  function measureStrip() {
    const app = ins.app, keep = ins.say?.dataset.show ?? 'empty';
    strip.style.height = '';
    strip.classList.add('measuring');
    let h = 0;
    for (const id of Object.keys(APPS)) {
      buildStrip(id);
      const marks = $('.say-marks', ins.say), clean = $('.say-clean', ins.say), lang = PROFILES[APPS[id].profile].lang;
      for (const lvl of Object.keys(LEVEL_NAMES)) {
        const text = written(id, lvl);
        marks.innerHTML = `<p>${text === SPOKEN ? esc(text) : marksHtml(text, lang)}</p>`;
        clean.innerHTML = cleanHtml(text);
        for (const what of ['marks', 'clean']) { ins.say.dataset.show = what; h = Math.max(h, strip.offsetHeight); }
      }
    }
    strip.classList.remove('measuring');
    strip.style.height = `${Math.ceil(h)}px`;
    ins.app = app; buildStrip(app); ins.shown = '';
    show(keep);
  }
  // The profile's light: the instrument's one range, a horizon under it.
  const saysGlowCanvas = $('#says-glow');
  const saysGlow = new Range(saysGlowCanvas, heroActor.voice, { ...HERO, lines: 24, row: 3, persp: .03, narrow: .008, base: .12, horizon: .96, amp: .24, rest: .3, swell: 9, width: .8, glow: 4 });
  watch.observe(saysGlowCanvas);
  watch.observe(says);
  let glowColour = hex(PROFILES.other.colour), glowTarget = glowColour;
  saysGlow.colours = tint(PROFILES.other.colour, undefined, HORIZON_WHITE);
  // A horizon, not a light source: it never blows out to white while the pill hears.
  saysGlow.look.gain = .75;
  let saysGlowAlpha = 1, saysT0 = null;
  // The first scroll-driven moment: the hero's night becomes the pill, over
  // 0.6 of a viewport. The intro then stays pinned for another half viewport
  // (style.css), so the first pass plays while the instrument holds still.
  const HANDOFF = .6;
  // Stop scrolling partway through the hand-off and, after a moment, it
  // finishes by itself, so no resting position shows an idle instrument.
  // Scrolling scrubs it as before; scrolling up hands the finish back.
  const SETTLE_FROM = .08, SETTLE_AFTER = 450, SETTLE_MS = 700;
  const settleState = { k: 0, goal: 0, y: -1, still: 0 };
  function settleHandoff(T, y, now, dt) {
    const S = settleState;
    if (y !== S.y) { if (y < S.y) S.goal = 0; S.y = y; S.still = now; }
    if (T >= .999) S.goal = S.k = 0;
    else if (T < SETTLE_FROM - .05) S.goal = 0;
    else if (T >= SETTLE_FROM && now - S.still > SETTLE_AFTER) S.goal = 1;
    S.k = clamp(S.k + Math.sign(S.goal - S.k) * dt / SETTLE_MS);
    return T + (1 - T) * easeInOut(S.k);
  }
  // Each part of the scene, and the T at which it starts to lift in.
  const saysReveal = [[$('#says-title'), .38], [appsRow, .46], [panel, .6], [$('.tune'), .66], [$('.says-foot'), .7]];
  buildStrip('anywhere'); showProfile();

  // 1b. Hold the key. The default gesture is hybrid (AppSettings.h): hold
  //     past 250 ms to talk until you let go, or tap to start and tap again
  //     to finish (ShortcutGesture.cpp). Held, the pill comes up at the
  //     bottom of the window and hears a sample while the ridge rises; let
  //     go, it refines, and the sentence lands under the headline, marked
  //     for a moment, then clean, then gone. Each press plays the next
  //     sample. Untouched for 6 s, the keys press themselves once.
  const SAMPLES = [
    { heard: 'book a table for six no wait four', marked: 'Book a table for <del>six, no wait,</del> four.', clean: 'Book a table for four.', tag: 'Sample' },
    { heard: 'get push origin main', marked: '<del>get</del> <ins>git</ins> push origin main', clean: 'git push origin main', tag: 'Sample, in a terminal', code: true },
    { heard: 'ask clerk to check the new logo', marked: 'Ask <del>clerk</del> <ins>Clark</ins> to check the new logo.', clean: 'Ask Clark to check the new logo.', tag: 'Sample, a word it learned' },
  ];
  const TAP = 250, FIRST_WORD = 260, WORD = 230, REFINE_HERO = 650, RECEIPT = 500, STAY = 4000, SELF = 3500;
  const talkButton = $('#talk'), talkCaps = $('#talk-caps'), talkLabel = $('.talk-label'), talkLive = $('#talk-live');
  const landed = $('#landed'), landedText = $('.landed-text', landed), landedTag = $('.landed-tag', landed);
  const heroPill = RM ? null : makePill($('.hero-pill'), heroActor.voice);
  // state: idle | held | latched (tapped on) | refining | landed. down is the
  // page's clock at the press (it paces the sample); pressedAt is the press
  // event's own time, which tells a hold from a tap even if frames stall.
  const talk = { state: 'idle', k: 0, at: 0, down: 0, pressedAt: 0, via: '', heard: 0, tapAt: 0, touched: false, self: false, lift: 0 };
  if (KEYS) {
    talkCaps.innerHTML = KEYS.caps.map((k) => `<kbd class="cap">${k}</kbd>`).join('');
    talkButton.setAttribute('aria-label', `Hold to talk, ${KEYS.name ?? KEYS.caps.join(' ')}: plays a sample dictation`);
  } else talkButton.classList.add('solo');
  const sample = () => SAMPLES[talk.k];
  function capsLook(down, lit) {
    talkButton.toggleAttribute('data-down', down);
    talkButton.toggleAttribute('data-lit', lit);
    labelEl.textContent = talk.state === 'latched' ? LATCHED : 'Hold to talk';
  }
  // The label's two texts take the same room, so the row never re-wraps between them.
  const LATCHED = 'Tap to finish', labelEl = KEYS ? talkLabel : $('.cap', talkCaps);
  function reserveLabel() {
    const keep = labelEl.textContent;
    labelEl.style.minWidth = '';
    const widths = [LATCHED, 'Hold to talk'].map((t) => { labelEl.textContent = t; return labelEl.getBoundingClientRect().width; });
    labelEl.textContent = keep;
    labelEl.style.minWidth = `${Math.ceil(Math.max(...widths))}px`;
  }
  // The line under the headline: a waiting caret, the marked edit, or the clean sentence.
  function showLanded(what) {
    const S = sample();
    landed.dataset.show = what;
    landed.classList.toggle('is-code', !!S.code);
    landedText.innerHTML = what === 'marks' ? S.marked : what === 'clean' ? esc(S.clean) : '';
    landedTag.textContent = what === 'marks' || what === 'clean' ? S.tag : '';
    heroCopy.classList.toggle('landing', what !== '');
  }
  // at: the event's timestamp (the page's clock for the keys' own press).
  function press(now, via, at = now) {
    if (via !== 'self') talk.touched = true;
    if (talk.state === 'latched') return finish(now);
    if (talk.state === 'held' || talk.state === 'refining') return;
    if (talk.state === 'landed') talk.k = (talk.k + 1) % SAMPLES.length;
    Object.assign(talk, { state: 'held', at: now, down: now, pressedAt: at, via, heard: 0 });
    capsLook(true, true);
    showLanded('wait');
    talkLive.textContent = '';
  }
  function release(now, via, at = now) {
    if (talk.state !== 'held' || via !== talk.via) return;
    // Reduced motion lands at once, whatever the gesture.
    if (!RM && at - talk.pressedAt <= TAP) { talk.state = 'latched'; capsLook(false, true); return; }
    finish(now);
  }
  function finish(now) {
    Object.assign(talk, { state: 'refining', at: now });
    capsLook(false, false);
    if (RM) landSample(now);
  }
  function landSample(now) {
    Object.assign(talk, { state: 'landed', at: now });
    showLanded('marks');
    talkLive.textContent = `${sample().tag}: ${sample().clean}`;
  }
  function stopTalk() {
    if (talk.state === 'idle') return;
    if (talk.state === 'landed') talk.k = (talk.k + 1) % SAMPLES.length;
    talk.state = 'idle';
    capsLook(false, false);
    showLanded('');
    heroPill?.set({ phase: 'hidden' });
  }
  const talking = () => talk.state === 'held' || talk.state === 'latched';
  function talkTick(now, dt) {
    talk.lift += ((talking() ? 1 : 0) - talk.lift) * (1 - Math.exp(-dt / 260));
    if (!talk.touched && !talk.self && talk.state === 'idle' && now - born > SELF && !document.hidden) { talk.self = true; press(now, 'self'); }
    const words = sample().heard.split(' '), t = now - talk.at;
    const lastWord = talk.down + FIRST_WORD + (words.length - 1) * WORD;
    if (talking()) {
      talk.heard = Math.min(words.length, Math.max(0, Math.floor((now - talk.down - FIRST_WORD) / WORD) + 1));
      heroPill.set({ phase: 'listening', words: words.slice(0, talk.heard), seconds: Math.floor((now - talk.down) / 1000) });
      // The keys let go of themselves once the sample is said; a tapped
      // dictation is finished by a second tap, shown on the keys.
      if (talk.via === 'self' && talk.state === 'held' && now > lastWord + 450) release(now, 'self');
      if (talk.state === 'latched' && now > lastWord + 900) {
        if (!talk.tapAt) { talk.tapAt = now; capsLook(true, true); }
        else if (now - talk.tapAt > 140) { talk.tapAt = 0; finish(now); }
      }
    } else if (talk.state === 'refining') {
      // Let go early and the rest of the words come in with the final transcript.
      if (t < REFINE_HERO) heroPill.set({ phase: 'refining', words });
      else { heroPill.set({ phase: 'delivered', receipt: { title: 'Sent to the app' } }); landSample(now); }
    } else if (talk.state === 'landed') {
      if (t > RECEIPT) heroPill.set({ phase: 'hidden' });
      if (t > 1200 && landed.dataset.show === 'marks') showLanded('clean');
      if (t > STAY) stopTalk();
    }
  }
  // Pointer and touch: hold the keys. Keyboard: Space or Enter on them, or
  // the shortcut itself anywhere while the hero is in view. A release counts
  // wherever it happens: focus may have moved, or the pointer left the caps.
  const clock = () => lastNow;
  // How long a press was held comes from the events' own times.
  const stamp = (e) => e.timeStamp;
  const heroLive = () => RM ? heroLayer.getBoundingClientRect().bottom > innerHeight * .5 : scrollY - introTop < H * .02;
  talkButton.addEventListener('pointerdown', (e) => {
    if (e.button) return;
    e.preventDefault();
    // Capture keeps the release ours if the finger slides off; it can fail for a pointer the browser no longer tracks.
    try { talkButton.setPointerCapture(e.pointerId); } catch {}
    press(clock(), 'pointer', stamp(e));
  });
  for (const type of ['pointerup', 'pointercancel']) addEventListener(type, (e) => release(clock(), 'pointer', stamp(e)), true);
  talkButton.addEventListener('lostpointercapture', (e) => release(clock(), 'pointer', stamp(e)));
  talkButton.addEventListener('contextmenu', (e) => e.preventDefault());
  const isHoldKey = (e) => e.key === ' ' || e.key === 'Enter';
  talkButton.addEventListener('keydown', (e) => { if (!isHoldKey(e)) return; e.preventDefault(); if (!e.repeat) press(clock(), 'key', stamp(e)); });
  // The key's release, wherever focus went meanwhile; it doesn't also press what now has focus.
  addEventListener('keyup', (e) => {
    if (!isHoldKey(e) || talk.via !== 'key' || !talking()) return;
    e.preventDefault();
    release(clock(), 'key', stamp(e));
  }, true);
  // A click with no pointer or key (a screen reader's) is a tap.
  talkButton.addEventListener('click', (e) => { if (e.detail === 0 && talk.state !== 'held') { press(clock(), 'click'); release(clock(), 'click'); } });
  if (KEYS) {
    addEventListener('keydown', (e) => {
      if (e.code !== 'KeyD' || !KEYS.held(e) || !heroLive()) return;
      e.preventDefault();
      if (!e.repeat) press(clock(), 'chord', stamp(e));
    });
    addEventListener('keyup', (e) => { if (e.code === 'KeyD' || !KEYS.held(e)) release(clock(), 'chord', stamp(e)); });
  }
  addEventListener('blur', () => release(clock(), talk.via, Infinity));

  // 4. Delivery: the page's one pinned layout, and its second scroll moment,
  //    in two beats for the heading's two claims. On desktops the stage holds
  //    still for 1.1 screens. It opens on a whole desktop: the pill hears
  //    you, and the words rise into the line at the cursor in Notes. Then the
  //    frame closes on Notes, half the stage, while the password field rides
  //    into the other half, where the words stop and go to the clipboard.
  //    Phones stack the two cards, and each plays its beat as it scrolls up
  //    into place. Every part is a function of scroll position, so it plays
  //    the same both ways and the words are whole at every point.
  const landsSec = $('#lands'), landsStage = $('#lands-stage'), landsScene = $('#lands-scene'), landsLead = $('#lands-lead');
  const cellA = $('.cell-a'), cellB = $('.cell-b'), landsFrame = $('#lands-frame');
  const notesWin = $('.w-notes', landsScene), loginWin = $('.w-login');
  const desktopOnly = [...$$('.w-side', landsScene), ...$$('.desk-chrome', cellA)];
  const guardDesk = $('.guard'), passwordField = $('.input-focus', guardDesk);
  const landsActor = actor(31, .5, 'babble'); actors.push(landsActor);
  const stagePill = (selector) => Object.assign(makePill($(selector), landsActor.voice, { scrub: true }), { stage: true });
  const landsPill = stagePill('.lands-pill'), copiedPill = stagePill('.cell-pill');
  watch.observe(landsStage);

  // Words in flight from a pill to a line in a window: one copy pale over the
  // night, one in ink over the paper, each clipped to its side of the
  // window's edge. at(rise, on): the words rise from the pill's words line
  // to the line, staggered.
  const easeRise = bezier(.3, 0, .15, 1);
  function flight(scene, win, line, pill) {
    const words = line.textContent.split(' ');
    line.innerHTML = words.map((w) => `<span class="w">${w}</span>`).join(' ');
    const layers = ['fly-pale', 'fly-ink'].map((c) => scene.appendChild(Object.assign(document.createElement('div'), { className: `fly ${c}` })));
    const flyers = layers.map((layer) => words.map((w) => layer.appendChild(Object.assign(document.createElement('span'), { textContent: w }))));
    const STAGGER = .03, RISE = 1 - STAGGER * (words.length - 1);
    let geo = null;
    const rect = ({ x, y, w, h }) => { const [l, t, r, b] = [x, y, x + w, y + h].map((v) => v.toFixed(1)); return `${l}px ${t}px, ${r}px ${t}px, ${r}px ${b}px, ${l}px ${b}px, ${l}px ${t}px`; };
    return {
      // Where each word ends, and where the pill's words line is: pill.js
      // starts words 44 app px in from the live shell's left, and the line's
      // centre is 23 below its top (stage pills hang from their top).
      measure() {
        const d = scene.getBoundingClientRect();
        const box = (el) => { const r = el.getBoundingClientRect(); return { x: r.left - d.left, y: r.top - d.top, w: r.width, h: r.height }; };
        const ends = $$('.w', line).map(box), m = box(pill.mount), w = box(win), k = pill.k;
        const start = { x: m.x + m.w / 2 - pill.wide * k / 2 + 44 * k, cy: m.y + 23 * k, s: 15 * k / parseFloat(getComputedStyle(line).fontSize) };
        layers[1].style.clipPath = `inset(${w.y.toFixed(1)}px ${(d.width - w.x - w.w).toFixed(1)}px ${(d.height - w.y - w.h).toFixed(1)}px ${w.x.toFixed(1)}px round 12px)`;
        layers[0].style.clipPath = `polygon(evenodd, 0 0, 100% 0, 100% 100%, 0 100%, 0 0, ${rect(w)})`;
        const font = getComputedStyle(line).font;
        for (const set of flyers) set.forEach((el, i) => Object.assign(el.style, { left: `${ends[i].x}px`, top: `${ends[i].y}px`, font, lineHeight: 'normal' }));
        geo = { ends, start };
      },
      // The words always take this position's pose, shown or not, so nothing
      // about them depends on how the page got here.
      at(rise, on) {
        for (const layer of layers) layer.classList.toggle('on', on);
        const { ends, start } = geo;
        ends.forEach((w, i) => {
          const t = span(rise, i * STAGGER, i * STAGGER + RISE), ex = easeInOut(t), ey = easeRise(t);
          const s = lerp(start.s, 1, ex);
          const dx = lerp(start.x + (w.x - ends[0].x) * start.s, w.x, ex) - w.x, dy = lerp(start.cy, w.y + w.h / 2, ey) - (w.y + w.h / 2);
          const transform = `translate(${dx.toFixed(1)}px, ${dy.toFixed(1)}px) scale(${s.toFixed(4)})`;
          for (const set of flyers) set[i].style.transform = transform;
        });
      },
    };
  }
  const landsFlight = flight(landsScene, notesWin, $('.lands-new'), landsPill);

  // Never in a password field. The words leave the pill as Speecher's own
  // object, a small capsule in the pill's ink, never as text on the form. It
  // rises until its top meets the field's bottom edge; there the capsule, not
  // the field, turns red, shows a lock and recoils. Then it shrinks into the
  // orb of the pill's Copied. The field stays blue and empty throughout: the
  // app leaves a secure field alone and only copies (TextDelivery.cpp).
  // pose({ rise, lock, recoil, fold, flare, on }): each 0 to 1, recoil in px.
  const LOCK = 18;      // the lock's slot: its 12 px glyph and a gap, times the stage's --z
  const MUTE = .5;      // folding, the words go here, at 80 %: never under 12 px
  const easeHit = bezier(.4, 0, .7, .85);   // still moving when it meets the field
  function capsule(scene, field, pill) {
    const el = $('.capsule', scene);
    let geo = null;
    return {
      measure() {
        const d = scene.getBoundingClientRect(), f = field.getBoundingClientRect(), m = pill.mount.getBoundingClientRect(), k = pill.k;
        el.style.transform = 'none'; el.style.setProperty('--lock', '0');
        const w = el.offsetWidth, h = el.offsetHeight, pad = parseFloat(getComputedStyle(el).paddingLeft);
        // The capsule's words are 15 px times --z; the pill's are 15 px times k.
        const z = parseFloat(getComputedStyle(el).fontSize) / 15, lock = LOCK * z;
        geo = { w, h, lock,
          // Its words start where the pill's did (44 app px in, the line's centre 23 below its top).
          start: { x: m.left - d.left + m.width / 2 - pill.wide * k / 2 + 44 * k - pad * k / z, cy: m.top - d.top + 23 * k, s: k / z },
          // Centred under the field with its lock, its top on the field's edge.
          rest: { x: f.left - d.left + (f.width - w - lock) / 2 + lock, cy: f.bottom - d.top + 1 + h / 2 },
          // The orb of the Copied receipt (160 wide).
          home: { x: m.left - d.left + m.width / 2 - 80 * k + 22 * k, cy: m.top - d.top + 22 * k } };
      },
      pose({ rise, lock, recoil, fold, flare, on }) {
        const { w, h, start, rest, home } = geo, ex = easeInOut(rise), f = easeInOut(fold);
        const width = w + geo.lock * lock, s0 = lerp(start.s, 1, ex);
        const s = s0 * lerp(1, .8, Math.min(1, f / MUTE)) * lerp(1, 24 / width, span(f, MUTE, 1));
        // Its centre, from the pill to the field, then to the orb.
        const left = lerp(start.x, rest.x, ex) - geo.lock * lock;
        const cx = lerp(left + width * s0 / 2, home.x, f);
        const cy = lerp(lerp(start.cy, rest.cy, easeHit(rise)) + recoil, home.cy, f);
        el.classList.toggle('on', on);
        el.classList.toggle('mute', f >= MUTE);
        el.style.transform = `translate(${(cx - width * s / 2).toFixed(1)}px, ${(cy - h / 2).toFixed(1)}px) scale(${s.toFixed(4)})`;
        el.style.opacity = (1 - span(f, .8, .98)).toFixed(3);
        for (const [name, v] of [['--lock', lock], ['--red', lock * (1 - span(f, .2, MUTE))], ['--edge', span(rise, 0, .25)], ['--flare', flare]]) el.style.setProperty(name, v.toFixed(3));
      },
    };
  }
  const guardCapsule = capsule(guardDesk, passwordField, copiedPill);

  // The two beats, each in its own time t (0..1).
  const WORKING = { phase: 'refining', words: [] };
  const HEAR = { phase: 'listening', words: 'pick up the dry cleaning before six'.split(' '), seconds: 2 };
  // The words have left the pill: it keeps its height until they are clear
  // of it, and only then refines.
  const heard = (s) => ({ ...s, words: [] });
  const SENT = { phase: 'delivered', receipt: { title: 'Sent to the app' } };
  const HEAR_GUARD = { phase: 'listening', words: 'running ten minutes late'.split(' '), seconds: 1 };
  const COPIED = { phase: 'delivered', receipt: { mark: 'copied', title: 'Copied' } };
  // Lands: hear, then the words rise into Notes while the pill refines.
  function landsBeat(t) {
    const LIFT = .28, LAND = .94;
    if (t < LIFT) landsPill.pose(HEAR, HEAR, 1);
    else if (t < LIFT + .28) landsPill.pose(HEAR, heard(HEAR), span(t, LIFT + .22, LIFT + .27), { cut: 0 });
    // It refines only once the words are clear of it.
    else if (t < LAND - .04) landsPill.pose(heard(HEAR), WORKING, span(t, LIFT + .28, LIFT + .36), { cut: 0 });
    else landsPill.pose(WORKING, SENT, span(t, LAND - .04, LAND + .04), { fadeIn: [.6, 1] });
    landsFlight.at(span(t, LIFT, LAND), t >= LIFT && t < LAND);
    const step = t >= LAND ? '1' : '0';
    if (landsScene.dataset.step !== step) landsScene.dataset.step = step;
    landsActor.talker.set(t < LIFT ? 'babble' : 'quiet');
  }
  // A pill hearing its words come in one at a time, x from 0 to 1.
  const hearing = (s, x) => { const n = s.words.length, state = { ...s, count: Math.min(n, Math.floor(x * (n + 1))) }; return [state, state, 1]; };
  // Guard: the pill hears, the capsule rises to the field and is refused,
  // then shrinks into the pill as it turns to Copied. The capsule flares at
  // the touch.
  const G = { HEARD: .12, LIFT: .16, TOUCH: .4, FOLD: [.52, .84], COPY: [.6, .72] };
  let guardFlare = 0;
  function guardBeat(t) {
    if (t < G.LIFT) copiedPill.pose(...hearing(HEAR_GUARD, t / G.HEARD));
    else if (t < G.LIFT + .16) copiedPill.pose(HEAR_GUARD, heard(HEAR_GUARD), span(t, G.LIFT + .1, G.LIFT + .15), { cut: 0 });
    // It refines only once the capsule is clear of it.
    else if (t < G.COPY[0]) copiedPill.pose(heard(HEAR_GUARD), WORKING, span(t, G.LIFT + .16, G.LIFT + .22), { cut: 0 });
    else copiedPill.pose(WORKING, COPIED, span(t, ...G.COPY));
    guardFlare = span(t, G.TOUCH, G.TOUCH + .01) - span(t, G.TOUCH + .01, G.TOUCH + .06);
    guardCapsule.pose({ rise: span(t, G.LIFT, G.TOUCH), lock: easeOut(span(t, G.TOUCH, G.TOUCH + .02)), recoil: 4 * easeOut(span(t, G.TOUCH, G.TOUCH + .05)),
      fold: span(t, ...G.FOLD), flare: guardFlare, on: t >= G.LIFT && t < G.FOLD[1] });
  }
  // The stage's geometry: its size, the two halves it ends in, and Notes
  // at its half's width throughout. --z sizes what is inside the cards, as
  // large as the stage's height allows (up to 1.5 times the old cards').
  const PILL_GAP = 24, LIFT_GAP = 96;
  const stageZ = () => parseFloat(landsStage.style.getPropertyValue('--z')) || 1;
  let stageBox = null, landsRect = null;
  const under = (win, gap) => `${(win.offsetTop + win.offsetHeight + gap).toFixed(1)}px`;
  function measureStage() {
    for (const el of [cellA, cellB, landsFrame, landsScene, notesWin, guardDesk, ...desktopOnly]) el.removeAttribute('style');
    const W = landsStage.clientWidth, H = landsStage.clientHeight, gap = 16;
    const col = (W - gap) / 2;
    stageBox = { W, H, gap, col };
    if (!phone()) {
      landsStage.style.setProperty('--z', clamp((H - 50) / 390, 1, 1.5).toFixed(3));
      Object.assign(cellB.style, { width: `${col}px`, height: `${H}px` });
      const nw = `${Math.min(640, col - 64).toFixed(0)}px`;
      notesWin.style.width = nw;
      landsScene.style.setProperty('--nw', nw);
    } else landsStage.style.removeProperty('--z');
    // The stage's pills grow with the cards (1.1 times the app's size at --z 1).
    const z = stageZ();
    for (const p of [landsPill, copiedPill]) { if (phone()) p.mount.style.removeProperty('--k'); else p.mount.style.setProperty('--k', (1.1 * z).toFixed(3)); p.measure(); }
    // Each pill hangs a set gap under its window; Notes' starts lower, so
    // the words have room to rise.
    landsPill.mount.style.top = under(notesWin, phone() ? 40 : LIFT_GAP);
    copiedPill.mount.style.top = under(loginWin, PILL_GAP);
  }
  // One position of the desktop stage, from pin progress p.
  const SPLIT = [.4, .54];
  // The guard's card rises into its half once the desktop has cleared it;
  // its window and pill appear whole when the frame is in, then settle the
  // last 10 px. Nothing with words crosses an edge.
  const ARRIVE = [.5, .58];
  function arrive(p, cell, desk, x, [a, b]) {
    const k = easeOut(span(p, a, b));
    Object.assign(cell.style, { transform: `translate(${x.toFixed(1)}px, ${((1 - k) * 28).toFixed(1)}px)`, opacity: k.toFixed(3) });
    desk.style.visibility = p >= b ? '' : 'hidden';
    desk.style.transform = `translateY(${((1 - easeOut(span(p, b, b + .03))) * 10).toFixed(1)}px)`;
  }
  function stageAt(p) {
    const { W, H, gap, col } = stageBox;
    // The desktop closes on Notes: its frame narrows to the first half and
    // the scene slides so Notes stays centred in what is left.
    const e = easeInOut(span(p, ...SPLIT));
    const width = lerp(W, col, e);
    cellA.style.clipPath = `inset(0 ${(W - width).toFixed(1)}px 0 0 round 20px)`;
    cellA.style.setProperty('--gx', `${(width / 2).toFixed(0)}px`);
    landsScene.style.transform = `translateX(${((width - W) / 2).toFixed(1)}px)`;
    const desk = (1 - easeOut(span(p, SPLIT[0], SPLIT[0] + .04))).toFixed(3);
    for (const el of desktopOnly) { el.style.opacity = desk; el.classList.toggle('bare', p > SPLIT[0]); }
    landsPill.mount.style.top = under(notesWin, lerp(LIFT_GAP, PILL_GAP, e));
    Object.assign(landsFrame.style, { width: `${width.toFixed(1)}px`, height: `${H}px` });
    arrive(p, cellB, guardDesk, col + gap, ARRIVE);
    landsRect = { width, height: H, e };
  }
  // The spotlight: while one card plays its beat the other sits under a
  // light night veil (its text stays above 4.5:1). dims = [A, B], 0 to 1.
  function spotlight(d) {
    [cellA, cellB].forEach((c, i) => c.style.setProperty('--dim', d[i].toFixed(3)));
  }
  // Pin progress: 0 as the stage pins, 1 as it lets go.
  const landsProgress = () => clamp(-landsSec.getBoundingClientRect().top / (landsSec.offsetHeight - innerHeight));
  const GUARD = [.58, .92];
  function landsAt(p) {
    stageAt(p);
    landsBeat(span(p, 0, .4));
    guardBeat(span(p, ...GUARD));
    const into = span(p, .54, .6), back = span(p, .92, .97);
    spotlight([into - back, 0]);
    // "And never anywhere it shouldn't." lights up as the capsule is refused.
    const hit = lerp(...GUARD, G.TOUCH);
    landsLead.style.setProperty('--lit', (span(p, hit - .01, hit + .02) - span(p, .92, .97)).toFixed(3));
  }
  // Phones: each card plays its beat while its window's top travels from
  // 65 % to 30 % of the screen's height, so it finishes where you read it.
  function cardBeat(card, win) {
    const top = card.getBoundingClientRect().top + win.offsetTop, vh = innerHeight;
    return span(top, vh * .65, vh * .3);
  }
  const landsPhoneRead = () => [cardBeat(cellA, notesWin), cardBeat(cellB, loginWin)];
  function landsPhoneAt([a, b]) { landsBeat(a); guardBeat(b); spotlight([0, 0]); }
  // Reduced motion ends with the refusal still in view: the capsule, locked,
  // under the untouched field, beside the pill's Copied.
  const landsEnd = () => {
    if (phone()) landsPhoneAt([1, 1]); else landsAt(1);
    guardCapsule.pose({ rise: 1, lock: 1, recoil: 4, fold: 0, flare: 0, on: true });
  };
  const landsMeasure = () => { measureStage(); landsFlight.measure(); guardCapsule.measure(); };
  // 5. No new account: the sign-up steps are struck out, one after another,
  //    the first time the line is well in view (once, on its own clock).
  //    The unstruck start is in the CSS from the first paint, so nothing
  //    plays backwards before it.
  const redline = $('#redline');
  if (!RM) {
    const strike = new IntersectionObserver(([e]) => {
      if (e.isIntersecting) { redline.classList.add('struck'); strike.disconnect(); }
    }, { threshold: .9 });
    strike.observe(redline);
  }

  // 5b. Android: the real flow, on its own clock while the phone is in view.
  //     Tap Speecher's button on the keyboard's mic key, its panel takes the
  //     keyboard's place and hears the words, Insert refined waits on the
  //     cleanup (the filled button spins, the words dim, DictationPanel.kt),
  //     shows the refined text, then commits it once into the field.
  const droid = $('#droid-screen'), droidSaid = $('#droid-said'), droidTyped = $('#droid-typed');
  const droidField = $('.compose-field', droid), droidSend = $('.compose-send', droid), droidFilled = $('.pb-filled', droid);
  const droidWave = $('#droid-wave');
  const DROID_SAID = 'Yeah, running about ten minutes late, um, save me a seat.'.split(' ');
  const DROID_CLEAN = 'Yeah, running about ten minutes late. Save me a seat!';
  const DROID_LOOP = 11500, DW0 = 2400, DWS = 330;   // loop; first word; one word every
  const droidBars = Array.from({ length: 28 }, () => droidWave.appendChild(document.createElement('i')));
  // The voice at time t: a bump per word, louder on some.
  const droidLevel = (t) => {
    const k = Math.floor((t - DW0) / DWS);
    if (k < 0 || k >= DROID_SAID.length) return 0;
    const x = (t - DW0 - k * DWS) / (DWS * .82);
    return x >= 1 ? 0 : Math.sin(Math.PI * x) ** .7 * (.55 + .45 * ((k * 7919) % 5) / 4);
  };
  let droidT0 = null, droidKey = '';
  function droidAt(t) {
    const step = t < 1500 ? 'type' : t < 1900 ? 'tap' : t < 6100 ? 'listen' : t < 7700 ? 'insert' : 'landed';
    const heard = Math.max(0, Math.min(DROID_SAID.length, Math.floor((t - DW0) / DWS) + 1));
    const phase = step !== 'insert' ? '' : t < 6400 ? 'press' : t < 7200 ? 'wait' : 'clean';
    const key = `${step}|${heard}|${phase}|${t > 11100}`;
    if (key !== droidKey) {
      droidKey = key;
      droid.dataset.step = step;
      droid.toggleAttribute('data-press', phase === 'press');
      droidFilled.classList.toggle('wait', phase === 'wait');
      droidSaid.classList.toggle('waiting', phase === 'wait');
      if (phase === 'clean') droidSaid.textContent = DROID_CLEAN;
      else if (heard === 0) droidSaid.innerHTML = '<span class="placeholder">Speak now</span>';
      else {
        // The newest word is still interim, in the softer colour, until the next one comes.
        const done = step === 'listen' && heard < DROID_SAID.length ? heard - 1 : heard;
        droidSaid.innerHTML = esc(DROID_SAID.slice(0, done).join(' ')) + (done < heard ? ` <span class="interim">${DROID_SAID[done]}</span>` : '');
      }
      const landed = step === 'landed';
      // The caret stays on the last word's line.
      const cut = DROID_CLEAN.lastIndexOf(' ');
      droidTyped.innerHTML = landed ? `${esc(DROID_CLEAN.slice(0, cut + 1))}<span class="nw">${esc(DROID_CLEAN.slice(cut + 1))}<i class="dcaret"></i></span>` : '';
      droidField.classList.toggle('has', landed);
      droidField.classList.toggle('fade', t > 11100);
      droidSend.classList.toggle('on', landed);
    }
    // The waveform: the last 28 moments of the voice, newest on the right;
    // silence is a row of dots.
    const live = step === 'listen';
    droidBars.forEach((b, i) => {
      const v = live ? droidLevel(t - (27 - i) * 70) : 0;
      b.style.height = `${(4 + v * 20).toFixed(1)}px`;
    });
  }
  function droidStill() {
    droid.dataset.step = 'listen';
    droidSaid.textContent = DROID_SAID.join(' ');
    droidBars.forEach((b, i) => { b.style.height = `${(4 + 16 * Math.sin(Math.PI * (i + .5) / 28) ** 1.5 * (.6 + .4 * Math.abs(Math.sin(i * 1.7)))).toFixed(1)}px`; });
  }
  watch.observe(droid);

  // 6. The terminal is set type; only its Copy button and one thin strip of
  //    the range run. The strip is the voice going into `speecher listen`,
  //    in the pill's P2 view and the terminal's violet. The CLI itself shows
  //    no pill (it runs headless), so the strip hangs from the subshell with
  //    the words you said, not as part of the terminal's output.
  const cmd = $('.cmd'), tty = $('#tty'), ttyVoice = $('#tty-voice');
  // Always talking, so the strip reads as a voice whenever you look.
  const ttyActor = actor(53, .55, 'babble'); actors.push(ttyActor);
  const ttyLook = (d) => ({ ...pillLook(d), rest: .14 });
  const ttyCanvas = $('#tty-strip');
  const TIP = 4;   // the band's left end, in from the canvas's edge
  const LEAD = 10; // where its lines visibly meet, which the leader drops to
  const ttyStrip = new Range(ttyCanvas, ttyActor.voice, ttyLook(1));
  ttyStrip.colours = tint('#B79CFF');
  watch.observe(ttyCanvas);
  // The row hangs from the subshell: it starts at the box's left edge, and
  // the leader drops from the box's bottom to the strip's tip on its centre line.
  const ttyLead = $('#tty-lead');
  function placeVoice() {
    const t = tty.getBoundingClientRect(), a = $('.sub-cmd').getBoundingClientRect();
    const x = Math.round(a.left - t.left);
    ttyVoice.style.setProperty('--x', `${x}px`);
    const s = ttyCanvas.getBoundingClientRect(), top = Math.round(a.bottom - t.top);
    Object.assign(ttyLead.style, { left: `${x + LEAD}px`, top: `${top}px`, height: `${Math.round(s.top + s.height / 2 - t.top) - top}px` });
  }

  // 8. The close: the range listening hard, wider and taller than the hero.
  const closeCanvas = $('#close-canvas');
  const closeActor = actor(67, .7, 'babble'); actors.push(closeActor);
  let closeHeadTop = 0;
  const closeRange = new Range(closeCanvas, closeActor.voice, { ...HERO, lines: 28, persp: .08, narrow: .03, amp: .62, horizon: .95, glow: 7 });
  watch.observe(closeCanvas);

  // The hover warp: near the cursor the lines bend gently away, never below
  // their own floor; the strength springs in and eases out over about 600 ms.
  // Fine pointers only, never with reduced motion.
  const FINE = !RM && matchMedia('(hover: hover) and (pointer: fine)').matches;
  const pointer = { x: -1e5, y: -1e5 };
  if (FINE) {
    addEventListener('pointermove', (e) => { pointer.x = e.clientX; pointer.y = e.clientY; }, { passive: true });
    root.addEventListener('pointerleave', () => { pointer.x = pointer.y = -1e5; });
  }
  const warps = [[hero, heroCanvas, 90], [saysGlow, saysGlowCanvas, 70], [closeRange, closeCanvas, 100]]
    .map(([range, canvas, radius]) => { range.warpRadius = radius; return { range, canvas, s: 0, v: 0, x: 0, y: 0 }; });
  function stepWarp(dt) {
    if (!FINE) return;
    const h = Math.min(dt, 50) / 1000;
    for (const w of warps) {
      if (!seen(w.canvas) && w.canvas !== heroCanvas) continue;
      if (w.s < .001 && pointer.x < -1e4) continue;
      const r = w.canvas.getBoundingClientRect();
      const inside = pointer.x >= r.left && pointer.x <= r.right && pointer.y >= r.top && pointer.y <= r.bottom;
      const k = inside ? 120 : 30, c = 2 * Math.sqrt(k) * (inside ? .7 : 1);
      w.v += (k * ((inside ? 1 : 0) - w.s) - c * w.v) * h;
      w.s = clamp(w.s + w.v * h, 0, 1.08);
      if (inside) {
        const f = w.s < .03 ? 1 : 1 - Math.exp(-dt / 90);
        w.x += (pointer.x - r.left - w.x) * f;
        w.y += (pointer.y - r.top - w.y) * f;
      }
      w.range.warp = { x: w.x, y: w.y, s: w.s };
    }
  }

  // Layout -------------------------------------------------------------------
  let W = 0, H = 0, introTop = 0;
  function layout() {
    W = stage.clientWidth; H = stage.clientHeight;
    introTop = intro.getBoundingClientRect().top + scrollY;
    hero.fit(density());
    reserveLabel();
    for (const r of [closeRange, saysGlow]) r.fit(density(1));
    measureStrip();
    // The instrument's light lies under its note, never behind words.
    const foot = $('.says-foot');
    saysGlowCanvas.style.height = `${Math.max(64, says.clientHeight - (foot.offsetTop + foot.offsetHeight) + 12)}px`;
    closeHeadTop = $('#download-title').getBoundingClientRect().top - closeCanvas.getBoundingClientRect().top;
    const sharp = density(2);
    ttyStrip.look = ttyLook(sharp); ttyStrip.fit(sharp);
    for (const p of pills) p.measure();
    // The stage is measured at its opening, then put where the scroll has it.
    landsMeasure();
    if (RM) landsEnd();
    else if (phone()) landsPhoneAt(landsPhoneRead());
    else landsAt(landsProgress());
    placeVoice();
    if (RM) settle();
  }
  // The pill the night becomes: its empty listening shape (280 x 44 app px),
  // fixed, so the hand-off is the same in both directions. It stands on the
  // composer on wide screens and hangs over it on phones (style.css).
  function handoffRect() {
    const m = saysPillMount.getBoundingClientRect(), k = saysPill.k;
    const w = 280 * k, h = 44 * k, cx = m.left + m.width / 2, top = phone() ? m.top : m.bottom - h;
    return { left: cx - w / 2, top, width: w, height: h, right: cx + w / 2, bottom: top + h };
  }

  // Reduced motion: every state at once, still.
  function settle() {
    heroLayer.style.visibility = '';
    saysPill.set(SENT);
    saysPill.settle();
    if (!ins.delivered) deliver(0);
    droidStill();
    for (const pill of [landsPill, copiedPill]) pill.settle();
    draw(0, true);
  }

  // One frame ----------------------------------------------------------------
  const nav = $('#nav'), saysTitle = $('#says-title');
  // dt is capped for the ranges' physics; pills morph on the real time (wall).
  function frame(dt, now, wall = dt) {
    lastNow = now;
    for (const a of actors) a.step(dt);
    stepWarp(dt);
    lean.x += (lean.tx - lean.x) * (1 - Math.exp(-dt / 600));
    lean.y += (lean.ty - lean.y) * (1 - Math.exp(-dt / 600));
    const y = scrollY;
    // Read before anything is written this frame.
    const lands = !seen(landsStage) ? null : phone() ? landsPhoneRead() : landsProgress();
    nav.classList.toggle('solid', y > 8);

    // 1 and 2 -------------------------------------------------------------
    const p = clamp((y - introTop) / H, 0, 1);
    // A whole-pixel scroll can stop a fraction short of the end: .999 is the end.
    let T = settleHandoff(span(p, 0, HANDOFF), y, now, Math.min(wall, 100));
    if (T >= .999) T = 1;
    if (seen(intro)) {
      // Until the night has landed, the pill waits empty: it is the shape the night becomes.
      if (T < 1) {
        if (saysT0 != null) { saysT0 = null; unland(); }
        saysPill.set({ phase: 'listening', words: [], seconds: 0 }); saysPill.settle();
      } else {
        // Land already hearing words, not idle; then the result, once. A fast
        // arrival past the pin, with the heading already under the nav, lands
        // on the delivered result instead of a pass nobody can watch.
        if (saysT0 == null) {
          saysT0 = now;
          const late = saysTitle.getBoundingClientRect().top < nav.getBoundingClientRect().bottom;
          land(late ? now - 1e4 : now);
        }
        instrumentAt(now);
      }
      instrumentTick(now, dt);
      // The keycap's sample plays only while the hero is up.
      if (T < .02) talkTick(now, dt); else stopTalk();
      heroActor.talker.set(talking() ? 'babble' : talk.state === 'refining' ? 'quiet' : (T < 1 || saysPill.phase === 'listening') ? 'phrase' : 'quiet');

      // The hero's copy sinks and goes quickly.
      const out = span(T, 0, .08);
      heroCopy.style.opacity = (1 - easeOut(out)).toFixed(3);
      heroCopy.style.transform = `translateY(${(easeInOut(out) * 60).toFixed(1)}px)`;
      heroCopy.style.visibility = out >= 1 ? 'hidden' : '';
      // The scene comes up only once the capsule has passed it, and can't take
      // keyboard focus while the hero covers it.
      for (const [el, from] of saysReveal) {
        const reveal = easeOut(span(T, from, from + .22));
        el.style.opacity = reveal.toFixed(3); el.style.transform = `translateY(${((1 - reveal) * 8).toFixed(1)}px)`;
      }
      says.inert = T < .7;
      saysGlowAlpha = easeOut(span(T, .5, 1));

      if (T < 1) {
        // The night gathers into the pill: it squashes toward the capsule's
        // height first, rounds as it goes, and the range folds into P2.
        heroLayer.style.visibility = '';
        const r = handoffRect(), k = saysPill.k;
        const ew = easeInOut(T), eh = easeOut(span(T, 0, .8)), eb = easeOut(span(T, .1, .85));
        const top0 = lerp(0, r.top, eh), bottom = lerp(H, r.bottom, eb);
        const cur = { x: lerp(0, r.left, ew), w: lerp(W, r.width, ew), y: top0, h: Math.max(r.height, bottom - top0) };
        const radius = Math.min(cur.h, cur.w) / 2 * easeOut(span(T, 0, .45));
        Object.assign(night.style, { left: `${cur.x.toFixed(1)}px`, top: `${cur.y.toFixed(1)}px`, width: `${cur.w.toFixed(1)}px`, height: `${cur.h.toFixed(1)}px`, borderRadius: `${radius.toFixed(1)}px` });
        night.style.borderColor = `rgba(238, 241, 234, ${(.12 * easeOut(span(T, .04, .3))).toFixed(3)})`;
        const g = span(T, .3, 1);
        night.style.backgroundColor = `rgb(${Math.round(lerp(12, 17, g))},${Math.round(lerp(15, 21, g))},${Math.round(lerp(13, 19, g))})`;
        saysPillMount.style.setProperty('--solid', '0');
        saysPillMount.style.setProperty('--chrome', easeOut(span(T, .75, 1)).toFixed(3));
        saysPill.visible = false;
        // The band keeps its place within the shape, from the hero's to the strip's.
        const hb = heroBox(W, H);
        const rel0 = { x: hb.x / W, cy: hb.cy / H, w: hb.w / W, h: hb.h / H };
        const rel1 = { x: 14 * k / r.width, cy: 22 * k / r.height, w: (280 - 118 - 14) * k / r.width, h: 28 * k / r.height };
        const bt = easeInOut(span(T, 0, .9));
        hero.layout(cur.x + lerp(rel0.x, rel1.x, bt) * cur.w, cur.y + lerp(rel0.cy, rel1.cy, bt) * cur.h,
          lerp(rel0.w, rel1.w, bt) * cur.w, lerp(rel0.h, rel1.h, bt) * cur.h);
        hero.scissor = { x: cur.x, y: cur.y, w: cur.w, h: cur.h };
        const look = lerpLook({ ...HERO, horizon: HERO.horizon - lean.y * .03 }, pillLook(saysPill.density), easeInOut(span(T, .15, 1)));
        // Assembly: the range recedes into being over the first seconds.
        const age = now - born;
        look.lines = Math.min(look.lines, 8 + 18 * easeOut(clamp(age / 1600)));
        // Held, the range stands taller, as it does for a louder voice.
        look.amp *= 1 + .3 * talk.lift;
        hero.look = look;
        hero.alpha = .6 + .4 * easeOut(clamp(age / 500));
        hero.render({ breath: .5 + .5 * Math.sin(now / 1000 * 1.85) });
      } else {
        heroLayer.style.visibility = 'hidden';
        saysPillMount.style.setProperty('--solid', '1');
        saysPillMount.style.setProperty('--chrome', '1');
        saysPill.visible = true;
      }
    }

    // The instrument's light eases to the profile's colour.
    if (seen(saysGlowCanvas)) {
      const f = 1 - Math.exp(-dt / 220);
      glowColour = glowColour.map((v, i) => v + (glowTarget[i] - v) * f);
      saysGlow.colours = tint(null, glowColour, HORIZON_WHITE);
    }

    // 4 -------------------------------------------------------------
    if (lands !== null) (phone() ? landsPhoneAt(lands) : landsAt(lands));
    if (seen(droid)) {
      if (droidT0 == null) droidT0 = now;
      droidAt((now - droidT0) % DROID_LOOP);
    } else droidT0 = null;

    draw(now);
    // The stage's pills follow the stage, not their own mounts, so their
    // geometry is the scroll position's alone.
    for (const pill of pills) {
      const on = pill.stage ? seen(landsStage) : seen(pill.mount);
      pill.mount.style.visibility = on ? '' : 'hidden';   // no shadow left under the nav
      if (pill === saysPill) pill.visible = pill.visible && on;
      else pill.visible = on;
      if (on) pill.frame(wall);
      else if (!pill.parked) { pill.visible = false; pill.draw(); }
      pill.parked = !on;
    }
  }

  function draw(now, still = false) {
    const breath = still ? 1 : .5 + .5 * Math.sin(now / 1000 * 1.85);
    if (seen(saysGlowCanvas) || still) {
      const w = saysGlowCanvas.clientWidth, h = saysGlowCanvas.clientHeight;
      saysGlow.alpha = still ? 1 : saysGlowAlpha;
      const gh = Math.min(h * .85, w * .42);
      saysGlow.layout(-w * .05, h - gh * .53, w * 1.1, gh);
      saysGlow.render({ breath, work: saysPill.phase === 'refining' && !still ? .55 : 0, sweep: now / 1100 });
    }
    if (seen(ttyCanvas) || still) {
      const w = ttyCanvas.clientWidth, h = ttyCanvas.clientHeight;
      ttyStrip.layout(TIP, h / 2, w - 2 * TIP, Math.min(h, 34));
      ttyStrip.render({ breath });
    }
    if (seen(closeCanvas) || still) {
      const w = closeCanvas.clientWidth, h = closeCanvas.clientHeight;
      // The range stands on the heading's top edge, never through it.
      const bw = phone() ? w * 1.3 : w * 1.04, bottom = closeHeadTop - 14;
      const bh = Math.max(120, Math.min(phone() ? 300 : 560, h * .72, bottom - 8));
      closeRange.layout((w - bw) / 2, bottom - bh / 2, bw, bh);
      closeRange.render({ breath });
    }
    if (still) {
      hero.look = { ...HERO };
      const b = heroBox(W, heroLayer.clientHeight);
      hero.scissor = null;
      hero.layout(b.x, b.cy, b.w, b.h);
      hero.render({ breath: 1 });
    }
  }

  // "How it works", the skip link and a shared #says link go to the end of the
  // hand-off, where the proof's heading is (it sits in the pinned stage).
  const toProof = () => {
    // Rounded up, so the hand-off is whole wherever 0.6 of the height isn't a whole pixel.
    if (!RM) scrollTo({ top: Math.ceil(introTop + H * HANDOFF), behavior: 'instant' });
    says.inert = false;
    $('#says-title').focus({ preventScroll: !RM });
  };
  for (const a of $$('a[href="#says"]')) a.addEventListener('click', (e) => { if (!RM) e.preventDefault(); history.replaceState(null, '', '#says'); toProof(); });

  // Download for this computer ---------------------------------------------
  (async () => {
    const RELEASE = 'https://github.com/firemonster612/speecher/releases/tag/v0.3.0';
    const ANDROID = 'https://github.com/firemonster612/speecher/releases/tag/android-v0.2.0';
    const FILES = {
      linux: ['Download for Linux', 'https://github.com/firemonster612/speecher/releases/download/v0.3.0/Speecher-x86_64.AppImage'],
      mac: ['Download for macOS', 'https://github.com/firemonster612/speecher/releases/download/v0.3.0/speecher.dmg'],
      windows: ['Download for Windows', 'https://github.com/firemonster612/speecher/releases/download/v0.3.0/Speecher-Setup-x64.exe'],
      phone: ['Get it for your computer', RELEASE],
      other: ['See all downloads', RELEASE],
    };
    const ua = navigator.userAgent;
    const platform = navigator.userAgentData?.platform || navigator.platform || '';
    let arch = /aarch64|arm/i.test(ua + platform) ? 'arm' : /i[3-6]86/i.test(ua + platform) ? 'x86-32' : '';
    try {
      if (navigator.userAgentData?.getHighEntropyValues) {
        const v = await navigator.userAgentData.getHighEntropyValues(['architecture', 'bitness']);
        if (v.architecture) arch = v.architecture === 'x86' && v.bitness === '32' ? 'x86-32' : v.architecture;
      }
    } catch {}
    // The Linux build is x86-64 only: ChromeOS, ARM and 32-bit Linux get the releases page.
    const os = /Android|iPhone|iPad|iPod/i.test(ua) || (/Mac/.test(platform) && navigator.maxTouchPoints > 1) ? 'phone'
      : /CrOS/.test(ua) ? 'other'
      : /Win/i.test(platform) ? 'windows'
      : /Mac/i.test(platform) ? 'mac'
      : /Linux|X11/i.test(platform + ua) ? (/arm|x86-32/i.test(arch) ? 'other' : 'linux') : null;
    if (!os) return;
    for (const button of [$('#hero-download'), $('#close-download')]) [button.textContent, button.href] = FILES[os];
    // What this computer needs, by the button, not only in the notes. The
    // AppImage needs glibc 2.43 (README.md).
    const NEEDS = { linux: 'Needs Ubuntu 26.04, Fedora 44, Debian 14 or newer.', mac: 'Needs macOS 15 or newer.', windows: 'Needs Windows 11.' };
    if (NEEDS[os]) {
      $('#hero-note').textContent = `Free and open source. ${NEEDS[os]}`;
      Object.assign($('#close-needs'), { textContent: NEEDS[os], hidden: false });
    }
    if (os === 'phone') $('#hero-note').innerHTML = `Free, open-source dictation for Linux, macOS and Windows. On Android, try the <a href="${ANDROID}">0.2.0 keyboard</a>.`;
    // "Also for" names only the platforms the button doesn't.
    const names = { linux: 'Linux', mac: 'macOS', windows: 'Windows' };
    if (names[os]) {
      const links = $$('#close-other a').slice(0, 3).filter((a) => a.textContent !== names[os]);
      const rest = $$('#close-other a').slice(3);
      $('#close-other').innerHTML = `Also for ${links.map((a) => a.outerHTML).join(' and ')}, a ${rest[0].outerHTML}, and the ${rest[1].outerHTML}.`;
    }
  })();

  // Copy the command: the Clipboard API where allowed (https), else the older
  // copy command on a selection, else select the text so it can be copied.
  // Focus comes back to the button either way.
  (() => {
    const button = $('#copy'), status = $('#copy-status');
    let timer = 0;
    // The label says what happened for 1.5 s, as the pill's Copied does.
    const say = (text) => {
      button.textContent = text; status.textContent = text; button.classList.toggle('done', text === 'Copied');
      clearTimeout(timer);
      timer = setTimeout(() => { button.textContent = 'Copy'; status.textContent = ''; button.classList.remove('done'); }, 1500);
    };
    button.addEventListener('click', async () => {
      const text = button.dataset.copy;
      try { await navigator.clipboard.writeText(text); return say('Copied'); } catch {}
      const area = document.createElement('textarea');
      area.value = text; area.setAttribute('readonly', ''); area.style.cssText = 'position:fixed;opacity:0;left:0;top:0';
      document.body.append(area); area.select();
      let ok = false;
      try { ok = document.execCommand('copy'); } catch {}
      area.remove();
      button.focus({ preventScroll: true });
      if (ok) return say('Copied');
      const range = document.createRange(); range.selectNodeContents(cmd);
      getSelection().removeAllRanges(); getSelection().addRange(range);
      say('Selected, press Ctrl+C');
    });
  })();

  // Run ---------------------------------------------------------------------
  ins.started = true;
  layout();
  addEventListener('resize', layout);
  document.fonts.ready.then(() => { layout(); if (location.hash === '#says') toProof(); });
  // The stage pills' strips start their GL while the page is idle, not on
  // the frame they first scroll into view.
  if (!RM) (window.requestIdleCallback ?? setTimeout)(() => { for (const pill of [landsPill, copiedPill]) pill.ensureRange(); });
  if (RM) {
    addEventListener('scroll', () => nav.classList.toggle('solid', scrollY > 8), { passive: true });
    return;
  }
  let last = performance.now();
  const loop = (t) => { frame(Math.min(50, t - last), t, Math.min(1000, t - last)); last = t; requestAnimationFrame(loop); };
  requestAnimationFrame(loop);
})();
