// The page's scenes. Two scroll-driven moments, the tagline's two halves: the
// hero's night becoming the pill ("Talk."), and the pinned delivery stage,
// where the words land in the window you were in and the layout opens into
// the three ways delivery behaves ("It lands."). Everything else scrolls
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
  const tint = (accent) => {
    const a = hex(accent);
    return { core: a.map((v) => v + (1 - v) * .82), far: a.map((v) => v * .7), halo: a };
  };

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
  let mic = null;
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

  // 2. The cleanup level, the app's own (Library.qml): one spoken sentence,
  //    marked as the app marks it, rewritten as you pick a level.
  const RAW = 'so um can you send me the the numbers from last quarter before friday thanks';
  const LEVELS = {
    none: { name: 'None', help: 'Pastes your words as spoken, unless the profile translates.',
      text: RAW, segs: [['same', RAW]] },
    light: { name: 'Light', help: 'Fixes punctuation, capitals and clear mistakes, and keeps your wording.',
      text: 'So, um, can you send me the numbers from last quarter before Friday? Thanks.',
      segs: [['same', 'So, um, can you send me the'], ['del', 'the'], ['same', 'numbers from last quarter before'], ['del', 'friday thanks'], ['ins', 'Friday? Thanks.']] },
    medium: { name: 'Medium', help: 'Also removes filler words and false starts, and adds paragraphs.',
      text: 'Can you send me the numbers from last quarter before Friday? Thanks.',
      segs: [['del', 'so um can'], ['ins', 'Can'], ['same', 'you send me'], ['del', 'the'], ['same', 'the numbers from last quarter before'], ['del', 'friday thanks'], ['ins', 'Friday? Thanks.']] },
    high: { name: 'High', help: 'Also rewrites for clarity, flow and organization, keeping the facts.',
      text: "Could you send me last quarter's numbers by Friday? Thank you.",
      segs: [['del', 'so um can'], ['ins', 'Could'], ['same', 'you send me'], ['del', 'the the numbers from last quarter before'], ['ins', "last quarter's numbers by"], ['del', 'friday thanks'], ['ins', 'Friday? Thank you.']] },
  };
  const said = RAW.split(' ');
  const proof = $('#proof'), proofSr = $('#proof-sr');
  let level = 'medium';
  function setLevel(id) {
    level = id;
    const L = LEVELS[id];
    proof.replaceChildren();
    L.segs.forEach(([kind, text], i) => {
      const node = document.createElement(kind === 'same' ? 'span' : kind);
      node.textContent = text;
      if (kind === 'del') { node.style.setProperty('--s', 1); node.classList.add('struck'); }
      if (kind === 'ins') node.style.setProperty('--v', 1);
      if (i) proof.append(' ');
      proof.append(node);
    });
    $('#level-name').textContent = L.name;
    $('#level-help').textContent = L.help;
    proofSr.textContent = `${L.name} cleanup writes: ${L.text}`;
  }
  radios($$('.levels [role="radio"]'), (b) => setLevel(b.dataset.level));
  setLevel('medium');
  const says = $('#says'), saysPillMount = $('.says-pill');
  // Each part of the scene, and the T at which it starts to lift in.
  const saysReveal = [[$('#says-title'), .38], [$('.says .lead'), .38], [$('.proof-frame'), .6], [$('.levels'), .7], [$('.level-help'), .7]];
  const saysPill = makePill(saysPillMount, heroActor.voice);
  // A far horizon under the control: the same voice, always a little lit.
  const saysGlowCanvas = $('#says-glow');
  const saysGlow = new Range(saysGlowCanvas, heroActor.voice, { ...HERO, lines: 24, row: 3, persp: .03, narrow: .008, base: .12, horizon: .96, amp: .24, rest: .3, swell: 9, width: .8, glow: 4 });
  watch.observe(saysGlowCanvas);
  let saysGlowAlpha = 1, saysT0 = null;
  // The first scroll-driven moment: the hero's night becomes the pill, over 0.6 of a viewport.
  const HANDOFF = .6;
  // The pill's own loop, once landed: hear the sentence, refine it, send it.
  // With cleanup at None there is no refining (DictationSession.cpp).
  const SAYS_LOOP = 7000;
  function saysState(t) {
    if (t < 2800) return { phase: 'listening', words: said, count: Math.max(1, Math.ceil(t / 2600 * said.length)), seconds: Math.floor(t / 1000) };
    if (t < 3800 && level !== 'none') return { phase: 'refining', words: LEVELS[level].text.split(' ') };
    if (t < 6400) return { phase: 'delivered', receipt: { title: 'Sent to the app' } };
    return { phase: 'listening', words: [], seconds: 0 };
  }

  // 3. The centrepiece: pick an app. Each app has its own window, and its
  //    profile's sample settings (tone, cleanup level, output language, all
  //    real settings) write the same spoken words differently. For a moment
  //    the edits show in red and blue, as the cleanup section marks them.
  const SPOKEN = "um so the upload test no wait the login test is failing again can you take a look don't change anything yet thanks";
  const COLOURS = { email: '#F2A65E', work: '#4FD1C5', ai: '#B79CFF', personal: '#FF8AB8' };
  const NAMES = { email: 'Email', work: 'Work', ai: 'AI coding', personal: 'Personal' };
  // segs: [kind, text] against the spoken words; clean: the result (\n\n = paragraph, `x` = code).
  const APPS = {
    mail: { name: 'Mail', profile: 'email', settings: 'Formal, High cleanup',
      segs: [['del', 'um so the upload test no wait the'], ['ins', 'The'], ['same', 'login test is failing again'], ['ins', '.'], ['del', 'can you take a look'], ['ins', 'Could you please take a look when you have a moment?'], ['del', "don't change anything yet thanks"], ['ins', "Please don't make any changes for now. Thank you."]],
      clean: "The login test is failing again. Could you please take a look when you have a moment?\n\nPlease don't make any changes for now. Thank you." },
    slack: { name: 'Slack', profile: 'work', settings: 'No tone, Medium cleanup',
      segs: [['del', 'um so the upload test no wait the'], ['ins', 'The'], ['same', 'login test is failing again'], ['ins', '.'], ['del', 'can'], ['ins', 'Can'], ['same', 'you take a look'], ['ins', '?'], ['del', "don't"], ['ins', "Don't"], ['same', 'change anything yet'], ['ins', '.'], ['del', 'thanks'], ['ins', 'Thanks.']],
      clean: "The login test is failing again. Can you take a look? Don't change anything yet. Thanks." },
    notion: { name: 'Notion', profile: 'work', settings: 'No tone, Light cleanup',
      segs: [['del', 'um so'], ['ins', 'Um, so'], ['del', 'the upload test no wait'], ['same', 'the login test is failing again'], ['ins', '.'], ['del', 'can'], ['ins', 'Can'], ['same', 'you take a look'], ['ins', '?'], ['del', "don't"], ['ins', "Don't"], ['same', 'change anything yet'], ['ins', '.'], ['del', 'thanks'], ['ins', 'Thanks.']],
      clean: "Um, so the login test is failing again. Can you take a look? Don't change anything yet. Thanks." },
    claude: { name: 'Claude Code', profile: 'ai', settings: 'No tone, Medium cleanup',
      segs: [['del', 'um so the upload test no wait the login test'], ['ins', '`tests/login_test.py`'], ['same', 'is failing again'], ['ins', '.'], ['del', 'can'], ['ins', 'Can'], ['same', 'you take a look'], ['ins', '?'], ['del', "don't"], ['ins', "Don't"], ['same', 'change anything yet'], ['ins', '.'], ['del', 'thanks'], ['ins', 'Thanks.']],
      clean: "`tests/login_test.py` is failing again. Can you take a look? Don't change anything yet. Thanks." },
    cursor: { name: 'Cursor', profile: 'ai', settings: 'No tone, Medium cleanup',
      segs: [['del', 'um so the upload test no wait the login test'], ['ins', '`test_login_redirect`'], ['same', 'is failing again'], ['ins', '.'], ['del', 'can'], ['ins', 'Can'], ['same', 'you take a look'], ['ins', '?'], ['del', "don't"], ['ins', "Don't"], ['same', 'change anything yet'], ['ins', '.'], ['del', 'thanks'], ['ins', 'Thanks.']],
      clean: "`test_login_redirect` is failing again. Can you take a look? Don't change anything yet. Thanks." },
    whatsapp: { name: 'WhatsApp', profile: 'personal', settings: 'Very casual, Medium cleanup',
      segs: [['del', 'um so the upload test no wait the login test is'], ['ins', "login test's"], ['same', 'failing again'], ['ins', '\u{1F629}'], ['same', 'can'], ['del', 'you'], ['ins', 'u'], ['same', 'take a look'], ['ins', '?'], ['same', "don't"], ['del', 'change'], ['ins', 'touch'], ['same', 'anything yet'], ['ins', 'tho,'], ['del', 'thanks'], ['ins', 'thx!']],
      clean: "login test's failing again \u{1F629} can u take a look? don't touch anything yet tho, thx!" },
    discord: { name: 'Discord', profile: 'personal', settings: 'Casual, Medium cleanup', lang: 'Writes in German', outLang: 'de',
      segs: [['del', SPOKEN], ['ins', 'Der Login-Test schlägt schon wieder fehl, kannst du mal draufschauen? Bitte noch nichts ändern, danke!']],
      clean: 'Der Login-Test schlägt schon wieder fehl, kannst du mal draufschauen? Bitte noch nichts ändern, danke!' },
  };
  const esc = (t) => t.replace(/[&<>]/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;' })[c]);
  const code = (t) => esc(t).replace(/`([^`]+)`/g, '<code>$1</code>');
  // A translated result is marked with its language; the struck original stays English.
  const langAttr = (A) => (A.outLang ? ` lang="${A.outLang}"` : '');
  const outHtml = (A) => {
    const lang = langAttr(A);
    const marks = A.segs.map(([k, t]) => (k === 'same' ? code(t) : `<${k}${k === 'ins' ? lang : ''}>${code(t)}</${k}>`)).join(' ').replace(/ <ins>([.,?])/g, '<ins>$1');
    const clean = A.clean.split('\n\n').map((p) => `<p>${code(p)}</p>`).join('').replace(/<\/p>$/, '<i class="caret"></i></p>');
    return `<div class="out" id="out"><div class="marks"><p>${marks}</p></div><div class="clean"${lang}>${clean}</div></div>`;
  };
  const WINDOWS = {
    mail: (o) => `<div class="app-win a-mail"><div class="tool"><span class="lights"><i></i><i></i><i></i></span><b>New Message</b><span class="send">&#x2191;</span></div>
      <p class="row"><span>To:</span>Sam Okafor</p><p class="row"><span>Subject:</span>Re: Login test</p><div class="body">${o}</div>
      <div class="quote"><b>On Monday, Sam Okafor wrote:</b>I pushed the session changes this morning. Let me know if anything breaks.</div></div>`,
    slack: (o) => `<div class="app-win a-slack"><div class="side"><span class="ws">Upload team</span><span class="ch"># general</span><span class="ch on"># backend</span><span class="ch"># releases</span><span class="sub">Direct messages</span><span class="ch">Rosa</span><span class="ch">Jun</span></div>
      <div class="main"><div class="head"># backend</div><div class="msgs"><div class="m"><span class="av b">R</span><div><b>Rosa</b><small>2:14 PM</small><br>deploy is out, ping me if anything looks off</div></div></div>
      <div class="compose">${o}</div></div></div>`,
    notion: (o) => `<div class="app-win a-notion"><p class="crumbs">Engineering / Release checklist</p><h4>Release checklist</h4>
      <p class="prop"><span>Status</span><span class="tag">In progress</span></p><p class="todo"><i></i>Ship the upload server</p><p class="todo"><i></i>Login test green</p>
      <div class="cmt"><span class="av">A</span><div>${o}</div></div></div>`,
    claude: (o) => `<div class="app-win a-claude dark"><div class="welcome"><b>&#x273B; Welcome to Claude Code</b><span class="cwd">cwd: ~/upload-service</span></div>
      <p class="dimt">&#x23FA; Bash(pytest tests/)</p><p class="err">&#x23BF; Last run: tests/login_test.py failed, 1 of 48</p>
      <div class="box"><span>&gt;</span><div>${o}</div></div><p class="foot">? for shortcuts</p></div>`,
    cursor: (o) => `<div class="app-win a-cursor dark"><div class="ed"><div class="tabs"><span class="tab on">login_test.py</span><span class="tab">auth.py</span></div><div class="code">
      <p class="ln"><i>12</i><span><span class="k">def</span> <span class="f">test_login_redirect</span>(client):</span></p>
      <p class="ln"><i>13</i><span>    r = client.post(<span class="s">"/login"</span>, data=USER)</span></p>
      <p class="ln"><i>14</i><span>    <span class="k">assert</span> r.status_code == 302</span></p>
      <p class="ln"><i>15</i><span>    <span class="k">assert</span> r.headers[<span class="s">"Location"</span>] == <span class="s">"/home"</span></span></p>
      <p class="ln"><i>16</i><span></span></p>
      <p class="ln"><i>17</i><span><span class="k">def</span> <span class="f">test_login_bad_password</span>(client):</span></p>
      <p class="ln"><i>18</i><span>    r = client.post(<span class="s">"/login"</span>, data=BAD)</span></p>
      <p class="ln"><i>19</i><span>    <span class="k">assert</span> r.status_code == 401</span></p>
      <p class="ln"><i>20</i><span></span></p>
      <p class="ln"><i>21</i><span><span class="k">def</span> <span class="f">test_logout</span>(client):</span></p>
      <p class="ln"><i>22</i><span>    <span class="k">assert</span> client.get(<span class="s">"/logout"</span>).status_code == 302</span></p></div></div>
      <div class="chat"><span class="chat-h">Chat</span><div class="turn"><b>Tests</b><br><span class="ok">47 passed</span>, <span class="bad">1 failed</span>: test_login_redirect</div>
      <div class="chat-in"><span class="ctx">@ login_test.py</span>${o}</div></div></div>`,
    whatsapp: (o) => `<div class="app-win a-wa dark"><div class="head"><span class="av">R</span><span>Rosa<small>online</small></span></div>
      <div class="chat"><div class="b in">did the fix go in?<span class="t">14:14</span></div></div><div class="compose">${o}</div></div>`,
    discord: (o) => `<div class="app-win a-discord dark"><div class="rail"><i class="on"></i><i></i><i></i></div><div class="chs"><b>Upload-Team</b><span># allgemein</span><span class="on"># dev</span><span># releases</span></div>
      <div class="main"><div class="head"># dev</div><div class="msgs"><div class="m"><span class="av"></span><div><span class="n1">Jonas</span><small>14:14</small><br><span lang="de">ist der Fix schon drin?</span></div></div></div>
      <div class="compose">${o}</div></div></div>`,
  };
  const pick = $('#pick'), pickStage = $('#pick-stage');
  let appWin = $('#app-win');
  const pickActor = actor(29, .5, 'phrase'); pickActor.talker.loopGap = 1800; actors.push(pickActor);
  const pickCanvas = $('#pick-glow');
  const pickGlow = new Range(pickCanvas, pickActor.voice, { ...HERO, lines: 16, row: 4, persp: .07, narrow: .02, amp: .42, rest: .25, glow: 6 });
  watch.observe(pickCanvas);
  watch.observe(pickStage);
  let pickColour = hex(COLOURS.email), pickTarget = pickColour, pickT0 = null;
  function showApp(b) {
    const A = APPS[b.dataset.app], colour = COLOURS[A.profile];
    pick.style.setProperty('--p', colour);
    pickStage.dataset.profile = A.profile;
    $('#result-profile').innerHTML = `<i class="dot"></i>${NAMES[A.profile]} profile`;
    $('#result-settings').textContent = A.settings;
    $('#result-lang').hidden = !A.lang;
    if (A.lang) $('#result-lang').textContent = A.lang;
    // Swap the window in place: the new one is whole from its first frame.
    appWin.outerHTML = WINDOWS[b.dataset.app](outHtml(A)).replace('class="app-win', 'id="app-win" class="app-win');
    appWin = $('#app-win');
    if (RM) $('#out').classList.add('is-clean');
    else appWin.animate([{ opacity: .85, transform: 'scale(.985)' }, { opacity: 1, transform: 'none' }], { duration: 300, easing: 'cubic-bezier(.165, .84, .44, 1)' });
    pickT0 = null;
    $('#result-sr').innerHTML = `${A.name}, ${NAMES[A.profile]} profile, sample settings ${A.settings}${A.lang ? ', ' + A.lang.toLowerCase() : ''}: <span${langAttr(A)}>${esc(A.clean.replace(/`/g, ''))}</span>`;
    pickTarget = hex(colour);
  }
  const pickButtons = $$('.dock [role="radio"]');
  // Keep the result you just asked for in view, by pointer or keyboard.
  const keepResultInView = () => {
    const r = pickStage.getBoundingClientRect(), navH = 64;
    if (r.top < navH) scrollBy({ top: r.top - navH - 8, behavior: RM ? 'instant' : 'smooth' });
  };
  radios(pickButtons, (b) => { showApp(b); keepResultInView(); });
  showApp(pickButtons[0]);

  // 4. Delivery: the page's one pinned layout, and its second scroll moment.
  //    On desktops the stage holds still for 1.25 screens. It opens on a
  //    whole desktop: the pill hears you, and the words rise into the line at
  //    the cursor in Notes. Then the frame closes on Notes while the two other
  //    cases ride in, and each acts out its beat as it settles: words stop at
  //    a password field and go to the clipboard, then an edit by voice opens
  //    its review. Phones stack the three cards, and each plays its beat as it
  //    scrolls up into place. Every part is a function of scroll position, so
  //    it plays the same both ways and the words are whole at every point.
  const landsSec = $('#lands'), landsStage = $('#lands-stage'), landsScene = $('#lands-scene'), landsLead = $('#lands-lead');
  const cellA = $('.cell-a'), cellB = $('.cell-b'), cellC = $('.cell-c'), landsFrame = $('#lands-frame');
  const notesWin = $('.w-notes', landsScene), loginWin = $('.w-login'), draftWin = $('.w-draft');
  const desktopOnly = [...$$('.w-side', landsScene), ...$$('.desk-chrome', cellA)];
  const desks = [$('.guard'), $('.edit-desk')];
  const guardDesk = $('.guard'), passwordField = $('.input-focus', guardDesk);
  const landsActor = actor(31, .5, 'babble'); actors.push(landsActor);
  const stagePill = (selector) => Object.assign(makePill($(selector), landsActor.voice, { scrub: true }), { stage: true });
  const landsPill = stagePill('.lands-pill'), copiedPill = stagePill('.cell-pill'), editPill = stagePill('.edit-pill');
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
  const LOCK = 18;      // the lock's slot: its 12 px glyph and a gap
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
        geo = { w, h,
          // Its words start where the pill's did (44 app px in, the line's centre 23 below its top).
          start: { x: m.left - d.left + m.width / 2 - pill.wide * k / 2 + (44 - pad) * k, cy: m.top - d.top + 23 * k, s: k },
          // Centred under the field with its lock, its top on the field's edge.
          rest: { x: f.left - d.left + (f.width - w - LOCK) / 2 + LOCK, cy: f.bottom - d.top + 1 + h / 2 },
          // The orb of the Copied receipt (160 wide).
          home: { x: m.left - d.left + m.width / 2 - 80 * k + 22 * k, cy: m.top - d.top + 22 * k } };
      },
      pose({ rise, lock, recoil, fold, flare, on }) {
        const { w, h, start, rest, home } = geo, ex = easeInOut(rise), f = easeInOut(fold);
        const width = w + LOCK * lock, s0 = lerp(start.s, 1, ex);
        const s = s0 * lerp(1, .8, Math.min(1, f / MUTE)) * lerp(1, 24 / width, span(f, MUTE, 1));
        // Its centre, from the pill to the field, then to the orb.
        const left = lerp(start.x, rest.x, ex) - LOCK * lock;
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

  // The three beats, each in its own time t (0..1).
  const WORKING = { phase: 'refining', words: [] };
  const HEAR = { phase: 'listening', words: 'pick up the dry cleaning before six'.split(' '), seconds: 2 };
  // The words have left the pill: it keeps its height until they are clear
  // of it, and only then refines.
  const heard = (s) => ({ ...s, words: [] });
  const SENT = { phase: 'delivered', receipt: { title: 'Sent to the app' } };
  const HEAR_GUARD = { phase: 'listening', words: 'running ten minutes late'.split(' '), seconds: 1 };
  const COPIED = { phase: 'delivered', receipt: { mark: 'copied', title: 'Copied' } };
  const EDIT_ASK = { phase: 'listening', words: ['change', 'Friday', 'to', 'Monday'], seconds: 1 };
  // The whole sentence is selected, so SelectionEditPresentation reports one change.
  const REVIEW = { phase: 'review', review: { said: '“change Friday to Monday”', summary: '1 change', segments: [
    { kind: 'same', text: 'Could you send me the numbers by ' }, { kind: 'removed', text: 'Friday' }, { kind: 'same', text: ' ' },
    { kind: 'added', text: 'Monday' }, { kind: 'same', text: '? Thanks.' }] } };
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
  // then shrinks into the pill as it turns to Copied. The cell's red light
  // flares at the touch.
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
  // Edit: the ask comes in, then the shell and the whole review take its
  // place together, at one point.
  function editBeat(t) {
    if (t < .3) editPill.pose(...hearing(EDIT_ASK, t / .26));
    else editPill.pose(EDIT_ASK, REVIEW, t >= .48 ? 1 : 0, { cut: 1 });
  }

  // The stage's geometry: its size, the three columns it ends in (the first
  // a little wider), and Notes at that first column's width throughout.
  const PILL_GAP = 24, LIFT_GAP = 96;
  let stageBox = null, landsRect = null;
  const under = (win, gap) => `${(win.offsetTop + win.offsetHeight + gap).toFixed(1)}px`;
  function measureStage() {
    const W = landsStage.clientWidth, H = landsStage.clientHeight, gap = 16;
    const colA = (W - 2 * gap) * .38, colW = (W - 2 * gap) * .31;
    stageBox = { W, H, gap, colA, colW };
    for (const el of [cellA, cellB, cellC, landsFrame, landsScene, notesWin, ...desks, ...desktopOnly]) el.removeAttribute('style');
    if (!phone()) {
      for (const c of [cellB, cellC]) Object.assign(c.style, { width: `${colW}px`, height: `${H}px` });
      const nw = `${Math.min(460, colA - 40).toFixed(0)}px`;
      notesWin.style.width = nw;
      landsScene.style.setProperty('--nw', nw);
    }
    // Each pill hangs a set gap under its window; Notes' starts lower, so
    // the words have room to rise.
    landsPill.mount.style.top = under(notesWin, phone() ? 40 : LIFT_GAP);
    copiedPill.mount.style.top = under(loginWin, PILL_GAP);
    editPill.mount.style.top = under(draftWin, PILL_GAP);
  }
  // One position of the desktop stage, from pin progress p.
  const SPLIT = [.36, .47];
  // Each card's frame and light rise into its own column once the desktop
  // has cleared it; its window and pill appear whole when the frame is in,
  // then settle the last 10 px. Nothing with words crosses an edge.
  const ARRIVE = { c: [.4, .47], b: [.455, .5] };
  function arrive(p, cell, desk, x, [a, b]) {
    const k = easeOut(span(p, a, b));
    Object.assign(cell.style, { transform: `translate(${x.toFixed(1)}px, ${((1 - k) * 28).toFixed(1)}px)`, opacity: k.toFixed(3) });
    desk.style.visibility = p >= b ? '' : 'hidden';
    desk.style.transform = `translateY(${((1 - easeOut(span(p, b, b + .025))) * 10).toFixed(1)}px)`;
  }
  function stageAt(p) {
    const { W, H, gap, colA, colW } = stageBox;
    // The desktop closes on Notes: its frame narrows to the first column and
    // the scene slides so Notes stays centred in what is left.
    const e = easeInOut(span(p, ...SPLIT));
    const width = lerp(W, colA, e);
    cellA.style.clipPath = `inset(0 ${(W - width).toFixed(1)}px 0 0 round 20px)`;
    cellA.style.setProperty('--gx', `${(width / 2).toFixed(0)}px`);
    landsScene.style.transform = `translateX(${((width - W) / 2).toFixed(1)}px)`;
    const desk = (1 - easeOut(span(p, SPLIT[0], SPLIT[0] + .035))).toFixed(3);
    for (const el of desktopOnly) { el.style.opacity = desk; el.classList.toggle('bare', p > SPLIT[0]); }
    landsPill.mount.style.top = under(notesWin, lerp(LIFT_GAP, PILL_GAP, e));
    Object.assign(landsFrame.style, { width: `${width.toFixed(1)}px`, height: `${H}px` });
    arrive(p, cellC, desks[1], colA + colW + 2 * gap, ARRIVE.c);
    arrive(p, cellB, desks[0], colA + gap, ARRIVE.b);
    landsRect = { width, height: H, e };
  }
  // The spotlight: while a card plays its beat the others sit under a light
  // night veil (their text stays above 4.5:1) and their light drops to a
  // quarter, and the edit pill waits for its own beat. dims = [A, B, C], 0
  // to 1 of the full spotlight.
  let dims = [0, 0, 0];
  const lightAt = (dim) => 1 - .65 * dim;   // under the veil's .72: a quarter at full
  function spotlight(d, held) {
    dims = d;
    [cellA, cellB, cellC].forEach((c, i) => c.style.setProperty('--dim', d[i].toFixed(3)));
    editPill.held = held;
    editPill.mount.style.opacity = held ? '0' : '';
  }
  // Pin progress: 0 as the stage pins, 1 as it lets go.
  const landsProgress = () => clamp(-landsSec.getBoundingClientRect().top / (landsSec.offsetHeight - innerHeight));
  const GUARD = [.5, .8], EDIT = [.82, .93];
  function landsAt(p) {
    stageAt(p);
    landsBeat(span(p, 0, .36));
    guardBeat(span(p, ...GUARD));
    editBeat(span(p, ...EDIT));
    const into = span(p, .47, .52), turn = span(p, .8, .84), back = span(p, .93, .98);
    spotlight([into - .45 * turn - .55 * back, .55 * turn - .55 * back, into - turn], p < EDIT[0]);
    // "And never anywhere it shouldn't." lights up as the capsule is refused.
    const hit = lerp(...GUARD, G.TOUCH);
    landsLead.style.setProperty('--lit', (span(p, hit - .01, hit + .02) - span(p, .8, .85)).toFixed(3));
  }
  // Phones: each card plays its beat while its window's top travels from
  // 65 % to 30 % of the screen's height, so it finishes where you read it.
  function cardBeat(card, win) {
    const top = card.getBoundingClientRect().top + win.offsetTop, vh = innerHeight;
    return span(top, vh * .65, vh * .3);
  }
  const landsPhoneRead = () => [cardBeat(cellA, notesWin), cardBeat(cellB, loginWin), cardBeat(cellC, draftWin)];
  function landsPhoneAt([a, b, c]) { landsBeat(a); guardBeat(b); editBeat(c); spotlight([0, 0, 0], false); }
  // Reduced motion ends with the refusal still in view: the capsule, locked,
  // under the untouched field, beside the pill's Copied.
  const landsEnd = () => {
    if (phone()) landsPhoneAt([1, 1, 1]); else landsAt(1);
    guardCapsule.pose({ rise: 1, lock: 1, recoil: 4, fold: 0, flare: 0, on: true });
  };
  const landsMeasure = () => { measureStage(); landsFlight.measure(); guardCapsule.measure(); };
  // The cells' lights: a voice for the first, a slow resting breath for the others.
  const restActor = actor(37, .4); actors.push(restActor);
  restActor.voice.reduced = true;
  let restT = 0;
  restActor.step = (dt) => { if (RM) return; restT += dt; restActor.voice.hist.fill(.42 + .14 * Math.sin(restT / 4200 * Math.PI * 2)); };
  const cellLook = { ...HERO, lines: 12, row: 5, persp: .08, amp: .5, rest: .3, width: 1.3, glow: 7 };
  const landsGlowCanvas = $('#lands-glow'), guardCanvas = $('#guard-glow'), editCanvas = $('#edit-glow');
  const landsGlow = new Range(landsGlowCanvas, landsActor.voice, { ...cellLook, amp: .32 });
  const guardGlow = new Range(guardCanvas, restActor.voice, { ...cellLook, lines: 10 });
  const editGlow = new Range(editCanvas, restActor.voice, { ...cellLook, lines: 10 });
  landsGlow.colours = tint('#6CCB8F'); guardGlow.colours = tint('#F0695C');
  for (const c of [landsGlowCanvas, guardCanvas, editCanvas]) watch.observe(c);
  // The review keeps the app's 500 px, and narrows (wrapping its buttons) to its cell.
  function fitEdit() {
    const m = $('.edit-pill'), inner = m.clientWidth;
    const k = Math.min(1.1, Math.max(1, inner / 520));
    m.style.setProperty('--k', k.toFixed(3));
    m.style.setProperty('--review-wide', String(Math.round(Math.min(500, inner / k))));
  }

  // 5. No new account: the real setup step's found sign-ins (WelcomeStep.qml
  //    lists only signed-in services), over a wide, low horizon of the range.
  const accountCanvas = $('#account-canvas'), setup = $('#setup');
  const accountActor = actor(41, .5, 'phrase'); accountActor.talker.loopGap = 2200; actors.push(accountActor);
  const account = new Range(accountCanvas, accountActor.voice, { ...HERO, lines: 30, persp: .06, narrow: .02, horizon: .9, amp: .3, rest: .2, glow: 5 });
  watch.observe(accountCanvas);
  let setupT0 = null;

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
  const warps = [[hero, heroCanvas, 90], [saysGlow, saysGlowCanvas, 70], [pickGlow, pickCanvas, 70], [landsGlow, landsGlowCanvas, 60], [guardGlow, guardCanvas, 50], [editGlow, editCanvas, 50],
    [account, accountCanvas, 100], [closeRange, closeCanvas, 100]]
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
    for (const r of [account, closeRange, saysGlow, landsGlow, guardGlow, editGlow, pickGlow]) r.fit(density(1));
    const sharp = density(2);
    ttyStrip.look = ttyLook(sharp); ttyStrip.fit(sharp);
    fitEdit();
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
  // fixed, so the hand-off is the same in both directions.
  function handoffRect() {
    const m = saysPillMount.getBoundingClientRect(), k = saysPill.k;
    const w = 280 * k, h = 44 * k, cx = m.left + m.width / 2;
    return { left: cx - w / 2, top: m.bottom - h, width: w, height: h, right: cx + w / 2, bottom: m.bottom };
  }

  // Reduced motion: every state at once, still.
  function settle() {
    heroLayer.style.visibility = '';
    saysPill.set({ phase: 'listening', words: said, count: said.length, seconds: 5 });
    saysPill.settle();
    setup.dataset.found = '2';
    droidStill();
    for (const pill of [landsPill, copiedPill, editPill]) pill.settle();
    draw(0, true);
  }

  // One frame ----------------------------------------------------------------
  const nav = $('#nav');
  function frame(dt, now) {
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
    const T = span(p, 0, HANDOFF);
    if (seen(intro)) {
      // Until the night has landed, the pill waits empty: it is the shape the night becomes.
      if (T < 1) { saysT0 = null; saysPill.set({ phase: 'listening', words: [], seconds: 0 }); saysPill.settle(); }
      else {
        // Land already hearing words, not idle.
        if (saysT0 == null) saysT0 = now - 900;
        saysPill.set(saysState((now - saysT0) % SAYS_LOOP));
      }
      heroActor.talker.set(mic ? 'quiet' : (T < 1 || saysPill.phase === 'listening') ? 'phrase' : 'quiet');

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

    // 3: the pick light eases to the chosen profile's colour.
    if (seen(pickCanvas)) {
      const f = 1 - Math.exp(-dt / 220);
      pickColour = pickColour.map((v, i) => v + (pickTarget[i] - v) * f);
      const c = pickColour;
      pickGlow.colours = { core: c.map((v) => v + (1 - v) * .82), far: c.map((v) => v * .7), halo: c };
    }

    // 4 and 5 -------------------------------------------------------------
    if (lands !== null) (phone() ? landsPhoneAt(lands) : landsAt(lands));
    if (seen(pickStage)) {
      if (pickT0 == null) pickT0 = now;
      const out = $('#out');
      if (out && !out.classList.contains('is-clean') && now - pickT0 > 1100) out.classList.add('is-clean');
    }
    if (seen(accountCanvas)) {
      if (setupT0 == null) setupT0 = now;
      const step = String((now - setupT0) < 300 ? 0 : (now - setupT0) < 800 ? 1 : 2);
      if (setup.dataset.found !== step) setup.dataset.found = step;
    } else setupT0 = null;
    if (seen(droid)) {
      if (droidT0 == null) droidT0 = now;
      droidAt((now - droidT0) % DROID_LOOP);
    } else droidT0 = null;

    draw(now);
    // The stage's pills follow the stage, not their own mounts, so their
    // geometry is the scroll position's alone.
    for (const pill of pills) {
      const on = pill.stage ? seen(landsStage) && !pill.held : seen(pill.mount);
      pill.mount.style.visibility = on ? '' : 'hidden';   // no shadow left under the nav
      if (pill === saysPill) pill.visible = pill.visible && on;
      else pill.visible = on;
      if (on) pill.frame(dt);
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
      saysGlow.render({ breath, work: saysPill.phase === 'refining' && !still ? 1 : 0, sweep: now / 1100 });
    }
    if (seen(pickCanvas) || still) {
      const w = pickCanvas.clientWidth, h = pickCanvas.clientHeight;
      pickGlow.layout(w * .04, h * .52, w * .92, h * .9);
      pickGlow.render({ breath });
    }
    // The first cell's light keeps low under its visible part, so its peaks
    // stay under the pill and the words rise over calm light.
    // Closed to its column, it takes the other cards' height of light.
    if (seen(landsGlowCanvas) || still) {
      const H = landsGlowCanvas.clientHeight, w = (phone() ? landsGlowCanvas.clientWidth : landsRect?.width ?? landsGlowCanvas.clientWidth) * 1.16;
      const e = phone() ? 0 : landsRect?.e ?? 0, h = H * lerp(.36, .62, e);
      landsGlow.layout(-w * .03, H - h + h * .58, w * .92, h * .8);
      landsGlow.alpha = lightAt(dims[0]);
      landsGlow.render({ breath });
    }
    // The refusal's flare: the guard light brightens for a moment at the touch.
    guardGlow.look.gain = 1 + 1.3 * guardFlare;
    guardGlow.look.amp = cellLook.amp * (1 + .35 * guardFlare);
    for (const [range, canvas, i] of [[guardGlow, guardCanvas, 1], [editGlow, editCanvas, 2]]) {
      if (!seen(canvas) && !still) continue;
      const w = canvas.clientWidth, h = canvas.clientHeight;
      range.alpha = lightAt(dims[i]);
      range.layout(w * .04, h * .58, w * .92, h * .8);
      range.render({ breath });
    }
    if (seen(ttyCanvas) || still) {
      const w = ttyCanvas.clientWidth, h = ttyCanvas.clientHeight;
      ttyStrip.layout(TIP, h / 2, w - 2 * TIP, Math.min(h, 34));
      ttyStrip.render({ breath });
    }
    if (seen(accountCanvas) || still) {
      const w = accountCanvas.clientWidth, h = accountCanvas.clientHeight;
      const bw = w * (phone() ? 1.6 : 1.15);
      account.layout((w - bw) / 2, h * .56, bw, h * .86);
      account.render({ breath });
    }
    if (seen(closeCanvas) || still) {
      const w = closeCanvas.clientWidth, h = closeCanvas.clientHeight;
      const bw = phone() ? w * 1.3 : w * 1.04;
      closeRange.layout((w - bw) / 2, h * .56, bw, Math.min(phone() ? 300 : 560, h * .72));
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
    if (!RM) scrollTo({ top: introTop + H * HANDOFF, behavior: 'instant' });
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
    if (os === 'phone') $('#hero-note').innerHTML = `Free, open-source dictation for Linux, macOS and Windows. On Android, try the <a href="${ANDROID}">0.2.0 keyboard</a>.`;
    // "Also for" names only the platforms the button doesn't.
    const names = { linux: 'Linux', mac: 'macOS', windows: 'Windows' };
    if (names[os]) {
      const links = $$('#close-other a').slice(0, 3).filter((a) => a.textContent !== names[os]);
      const rest = $$('#close-other a').slice(3);
      $('#close-other').innerHTML = `Also for ${links.map((a) => a.outerHTML).join(' and ')}, a ${rest[0].outerHTML}, and the ${rest[1].outerHTML}.`;
    }
  })();

  // Try it with your voice: only where the browser allows a microphone. The
  // page reads one loudness number a frame and throws the audio away.
  (() => {
    const button = $('#voice-try');
    if (RM || !window.isSecureContext || !navigator.mediaDevices?.getUserMedia) return;
    button.hidden = false;
    let stream = null, context = null;
    const note = $('#hero-note'), noteText = note.innerHTML;
    function stop() {
      stream?.getTracks().forEach((t) => t.stop()); context?.close();
      stream = context = mic = null;
      button.textContent = 'Try it with your voice'; note.innerHTML = noteText;
    }
    button.addEventListener('click', async () => {
      if (stream) return stop();
      try { stream = await navigator.mediaDevices.getUserMedia({ audio: { echoCancellation: false, noiseSuppression: true } }); } catch { stream = null; return; }
      context = new AudioContext();
      const analyser = context.createAnalyser(); analyser.fftSize = 1024;
      context.createMediaStreamSource(stream).connect(analyser);
      const samples = new Float32Array(analyser.fftSize);
      mic = () => { analyser.getFloatTimeDomainData(samples); let s = 0; for (const v of samples) s += v * v; return Math.min(1, Math.sqrt(s / samples.length) * 8); };
      button.textContent = 'Stop listening';
      note.textContent = 'Listening. Nothing is recorded or sent.';
    });
    const step = heroActor.step;
    heroActor.step = (dt) => { if (!mic) return step(dt); heroActor.talker.update(dt); heroActor.voice.update(dt, mic()); };
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
  layout();
  addEventListener('resize', layout);
  document.fonts.ready.then(() => { layout(); if (location.hash === '#says') toProof(); });
  // The stage pills' strips start their GL while the page is idle, not on
  // the frame they first scroll into view.
  if (!RM) (window.requestIdleCallback ?? setTimeout)(() => { for (const pill of [landsPill, copiedPill, editPill]) pill.ensureRange(); });
  if (RM) {
    addEventListener('scroll', () => nav.classList.toggle('solid', scrollY > 8), { passive: true });
    return;
  }
  let last = performance.now();
  const loop = (t) => { frame(Math.min(50, t - last), t); last = t; requestAnimationFrame(loop); };
  requestAnimationFrame(loop);
})();
