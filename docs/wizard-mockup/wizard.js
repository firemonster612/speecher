/* Speecher setup wizard mockup — shared behaviour.
 *
 * Three jobs:
 *   1. build the icon sprite (all SVG geometry lives here, one copy),
 *   2. apply the ?state= variant declaratively via data- attributes,
 *   3. a little inert interactivity (provider cards, level meter, copy).
 *
 * No frameworks, no network, no alert/confirm/prompt.
 */

/* ------------------------------------------------------------------ icons */

/* The two brand marks are the real ones, copied from assets/brand/. They keep
 * their own viewBox and their own colours: at 20px the teal-and-white ChatGPT
 * tile and the #D97757 Claude burst are what people actually recognise, and a
 * currentColor silhouette of either loses most of that. Everything else in
 * this map is a plain currentColor line icon on the 24x24 grid.
 *
 * An entry is either a body string (24x24 assumed) or {viewBox, body}. */

/* assets/brand/chatgpt.svg — the inner cell is stamped six times by <use>,
 * so its id is namespaced to survive living in a shared sprite. */
const CHATGPT =
  `<path d="M1 578.4C1 259.5 259.5 1 578.4 1h1249.1c319 0 577.5 258.5 577.5 577.4V2406H578.4C259.5 2406 1 2147.5 1 1828.6V578.4z" fill="#74aa9c"/>` +
  `<path id="chatgpt-cell" d="M1107.3 299.1c-197.999 0-373.9 127.3-435.2 315.3L650 743.5v427.9c0 21.4 11 40.4 29.4 51.4l344.5 198.515V833.3h.1v-27.9L1372.7 604c33.715-19.52 70.44-32.857 108.47-39.828L1447.6 450.3C1361 353.5 1237.1 298.5 1107.3 299.1zm0 117.5-.6.6c79.699 0 156.3 27.5 217.6 78.4-2.5 1.2-7.4 4.3-11 6.1L952.8 709.3c-18.4 10.4-29.4 30-29.4 51.4V1248l-155.1-89.4V755.8c-.1-187.099 151.601-338.9 339-339.2z" fill="#fff"/>` +
  [60, 120, 180, 240, 300]
    .map((deg) => `<use href="#chatgpt-cell" transform="rotate(${deg} 1203 1203)"/>`)
    .join('');

/* assets/brand/claude.svg */
const CLAUDE =
  `<path fill="#D97757" d="m4.7144 15.9555 4.7174-2.6471.079-.2307-.079-.1275h-.2307l-.7893-.0486-2.6956-.0729-2.3375-.0971-2.2646-.1214-.5707-.1215-.5343-.7042.0546-.3522.4797-.3218.686.0608 1.5179.1032 2.2767.1578 1.6514.0972 2.4468.255h.3886l.0546-.1579-.1336-.0971-.1032-.0972L6.973 9.8356l-2.55-1.6879-1.3356-.9714-.7225-.4918-.3643-.4614-.1578-1.0078.6557-.7225.8803.0607.2246.0607.8925.686 1.9064 1.4754 2.4893 1.8336.3643.3035.1457-.1032.0182-.0728-.164-.2733-1.3539-2.4467-1.445-2.4893-.6435-1.032-.17-.6194c-.0607-.255-.1032-.4674-.1032-.7285L6.287.1335 6.6997 0l.9957.1336.419.3642.6192 1.4147 1.0018 2.2282 1.5543 3.0296.4553.8985.2429.8318.091.255h.1579v-.1457l.1275-1.706.2368-2.0947.2307-2.6957.0789-.7589.3764-.9107.7468-.4918.5828.2793.4797.686-.0668.4433-.2853 1.8517-.5586 2.9021-.3643 1.9429h.2125l.2429-.2429.9835-1.3053 1.6514-2.0643.7286-.8196.85-.9046.5464-.4311h1.0321l.759 1.1293-.34 1.1657-1.0625 1.3478-.8804 1.1414-1.2628 1.7-.7893 1.36.0729.1093.1882-.0183 2.8535-.607 1.5421-.2794 1.8396-.3157.8318.3886.091.3946-.3278.8075-1.967.4857-2.3072.4614-3.4364.8136-.0425.0304.0486.0607 1.5482.1457.6618.0364h1.621l3.0175.2247.7892.522.4736.6376-.079.4857-1.2142.6193-1.6393-.3886-3.825-.9107-1.3113-.3279h-.1822v.1093l1.0929 1.0686 2.0035 1.8092 2.5075 2.3314.1275.5768-.3218.4554-.34-.0486-2.2039-1.6575-.85-.7468-1.9246-1.621h-.1275v.17l.4432.6496 2.3436 3.5214.1214 1.0807-.17.3521-.6071.2125-.6679-.1214-1.3721-1.9246L14.38 17.959l-1.1414-1.9428-.1397.079-.674 7.2552-.3156.3703-.7286.2793-.6071-.4614-.3218-.7468.3218-1.4753.3886-1.9246.3157-1.53.2853-1.9004.17-.6314-.0121-.0425-.1397.0182-1.4328 1.9672-2.1796 2.9446-1.7243 1.8456-.4128.164-.7164-.3704.0667-.6618.4008-.5889 2.386-3.0357 1.4389-1.882.929-1.0868-.0062-.1579h-.0546l-6.3385 4.1164-1.1293.1457-.4857-.4554.0608-.7467.2307-.2429 1.9064-1.3114Z"/>`;

const ICONS = {
  openai: { viewBox: '0 0 2406 2406', body: CHATGPT },

  anthropic: { viewBox: '0 0 24 24', body: CLAUDE },

  mic:
    `<g fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round">` +
    `<rect x="9" y="2.8" width="6" height="10.4" rx="3"/>` +
    `<path d="M5.8 11.2a6.2 6.2 0 0 0 12.4 0"/>` +
    `<path d="M12 17.4v3.2"/><path d="M8.6 20.6h6.8"/></g>`,

  keyboard:
    `<g fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round">` +
    `<rect x="2.4" y="5.6" width="19.2" height="12.8" rx="2.2"/>` +
    `<path d="M6 9.6h.01M9.4 9.6h.01M12.8 9.6h.01M16.2 9.6h.01M19 9.6h.01"/>` +
    `<path d="M6 13h.01M9.4 13h.01M19 13h.01"/>` +
    `<path d="M8.6 15.9h6.8"/></g>`,

  check:
    `<g fill="none" stroke="currentColor" stroke-width="1.9" stroke-linecap="round" stroke-linejoin="round">` +
    `<path d="M4.8 12.6 9.6 17.4 19.2 6.8"/></g>`,

  checkCircle:
    `<g fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round">` +
    `<circle cx="12" cy="12" r="8.6"/><path d="M8 12.3 11 15.3 16.2 8.9"/></g>`,

  warning:
    `<g fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round">` +
    `<path d="M12 3.9 21.3 19.6a1 1 0 0 1-.87 1.5H3.57a1 1 0 0 1-.87-1.5Z"/>` +
    `<path d="M12 9.6v4.6"/><path d="M12 17.4h.01"/></g>`,

  pending:
    `<g fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round">` +
    `<circle cx="12" cy="12" r="8.6"/><path d="M8.2 12h7.6"/></g>`,

  spinner:
    `<g fill="none" stroke="currentColor" stroke-width="1.7" stroke-linecap="round">` +
    `<circle cx="12" cy="12" r="8.4" opacity=".22"/>` +
    `<path class="spinner" d="M12 3.6a8.4 8.4 0 0 1 8.4 8.4"/></g>`,

  copy:
    `<g fill="none" stroke="currentColor" stroke-width="1.6" stroke-linejoin="round">` +
    `<rect x="8.4" y="8.4" width="11.8" height="11.8" rx="2.2"/>` +
    `<path d="M15.6 5.6a2.2 2.2 0 0 0-2.2-2.2H6a2.2 2.2 0 0 0-2.2 2.2v7.4a2.2 2.2 0 0 0 2.2 2.2"/></g>`,

  arrow:
    `<g fill="none" stroke="currentColor" stroke-width="1.7" stroke-linecap="round" stroke-linejoin="round">` +
    `<path d="M4.6 12h13.6"/><path d="M13 6.8 18.2 12 13 17.2"/></g>`,

  cursor:
    `<g fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round">` +
    `<path d="M5.6 3.4 18.6 11.2l-5.6 1.4-2.2 5.5Z"/><path d="M13.6 14.2 19 20"/></g>`,
};

function buildSprite() {
  const symbols = Object.entries(ICONS)
    .map(([name, icon]) => {
      const viewBox = typeof icon === 'string' ? '0 0 24 24' : icon.viewBox;
      const body = typeof icon === 'string' ? icon : icon.body;
      return `<symbol id="icon-${name}" viewBox="${viewBox}">${body}</symbol>`;
    })
    .join('');
  const host = document.createElement('div');
  host.className = 'svg-sprite';
  host.setAttribute('aria-hidden', 'true');
  host.innerHTML = `<svg xmlns="http://www.w3.org/2000/svg">${symbols}</svg>`;
  document.body.prepend(host);
}

/* ------------------------------------------------------------------ state */

const PAGES = [
  'welcome.html',
  'transcription.html',
  'microphone.html',
  'accessibility.html',
  'delivery.html',
  'refinement.html',
  'profiles.html',
  'shortcut.html',
  'ready.html',
];

function currentState() {
  const asked = new URLSearchParams(location.search).get('state');
  const allowed = (document.body.dataset.states || '').split(/\s+/).filter(Boolean);
  if (asked && (!allowed.length || allowed.includes(asked))) return asked;
  return document.body.dataset.defaultState || (allowed[0] || 'default');
}

function matches(list, state) {
  return list.split(/\s+/).filter(Boolean).includes(state);
}

function applyState(state) {
  document.body.dataset.state = state;

  document.querySelectorAll('[data-show]').forEach((el) => {
    el.hidden = !matches(el.dataset.show, state);
  });
  document.querySelectorAll('[data-hide]').forEach((el) => {
    if (matches(el.dataset.hide, state)) el.hidden = true;
  });
  document.querySelectorAll('[data-disabled-in]').forEach((el) => {
    el.disabled = matches(el.dataset.disabledIn, state);
  });
  document.querySelectorAll('[data-checked-in]').forEach((el) => {
    if (matches(el.dataset.checkedIn, state)) el.checked = true;
  });
  document.querySelectorAll('[data-text-for]').forEach((el) => {
    const map = JSON.parse(el.dataset.textFor);
    if (state in map) el.textContent = map[state];
  });
}

/* --------------------------------------------------------- navigation ---- */

function wireNav() {
  const here = location.pathname.split('/').pop() || 'welcome.html';
  const at = PAGES.indexOf(here);
  document.querySelectorAll('[data-nav]').forEach((btn) => {
    const dir = btn.dataset.nav;
    const target =
      dir === 'back' ? PAGES[at - 1] : dir === 'next' ? PAGES[at + 1] : null;
    btn.addEventListener('click', () => {
      if (btn.disabled) return;
      if (target) location.href = target;
    });
  });
  document.querySelectorAll('[data-goto]').forEach((btn) => {
    btn.addEventListener('click', () => {
      location.href = btn.dataset.goto;
    });
  });
}

/* ------------------------------------------------------ provider choices */

function statusIcon(tone) {
  if (tone === 'ok') return 'check';
  if (tone === 'checking') return 'spinner';
  if (tone === 'warn') return 'warning';
  return 'pending';
}

function setStatus(el, label, tone) {
  el.className = 'status' + (tone && tone !== 'checking' ? ' ' + tone : '');
  el.innerHTML =
    `<svg class="icon sm" aria-hidden="true"><use href="#icon-${statusIcon(tone)}"/></svg>` +
    `<span>${label}</span>`;
}

function renderStats(table, stats, name) {
  if (!table) return;
  if (!stats || !stats.length) {
    table.hidden = true;
    return;
  }
  table.hidden = false;
  const caption = table.querySelector('caption');
  if (caption) caption.textContent = name;
  table.querySelector('tbody').innerHTML = stats
    .map(([k, v]) => `<tr><th scope="row">${k}</th><td>${v}</td></tr>`)
    .join('');
}

function wireChoiceGroups(state) {
  document.querySelectorAll('[data-choicegroup]').forEach((group) => {
    const choices = [...group.querySelectorAll('.choice')];
    const panel = document.querySelector(group.dataset.panel || '#detail');
    if (!panel) return;
    const table = panel.querySelector('.stats');
    const statusLine = panel.querySelector('.js-status');
    const hint = panel.querySelector('.js-hint');
    const recheck = panel.querySelector('.js-recheck');
    const warnBox = panel.querySelector('.js-warning');
    const next = document.querySelector('[data-nav="next"]');
    const gated = group.dataset.gate === 'true';

    const read = (choice) => {
      const all = JSON.parse(choice.dataset.states || '{}');
      return all[state] || all['*'] || {};
    };

    choices.forEach((choice) => {
      const info = read(choice);
      const badge = choice.querySelector('.choice-status');
      if (badge) {
        badge.textContent = info.status || '';
        badge.className =
          'choice-status' + (info.tone === 'ok' ? ' ok' : info.tone === 'warn' ? ' warn' : '');
      }
    });

    const update = () => {
      const choice = choices.find((c) => c.querySelector('input').checked);
      if (!choice) return;
      const info = read(choice);
      const name = choice.dataset.name || '';
      renderStats(table, JSON.parse(choice.dataset.stats || '[]'), name + ' at a glance');

      if (statusLine) {
        if (info.line === null || info.line === undefined) statusLine.hidden = true;
        else {
          statusLine.hidden = false;
          setStatus(statusLine, info.line, info.tone);
        }
      }
      const needsHelp = info.tone !== 'ok' && choice.dataset.hint;
      if (hint) {
        hint.hidden = !needsHelp;
        hint.textContent = choice.dataset.hint || '';
      }
      if (recheck) recheck.hidden = !needsHelp;
      if (warnBox) {
        const show = Boolean(info.warning);
        warnBox.hidden = !show;
        const text = warnBox.querySelector('p');
        if (show && text) text.textContent = info.warning;
      }
      if (gated && next) next.disabled = info.tone !== 'ok';
    };

    choices.forEach((choice) => {
      choice.querySelector('input').addEventListener('change', update);
    });
    update();
  });
}

/* ------------------------------------------------------------ level meter */

function wireMeter(state) {
  const meter = document.querySelector('[data-meter]');
  if (!meter) return;
  const fill = meter.querySelector('.meter-fill');
  const value = document.querySelector('.meter-value');
  const quiet = state === 'silent' || state === 'nomic';

  if (quiet) {
    fill.classList.add('quiet');
    fill.style.width = '0%';
    if (value) value.textContent = '0%';
    return;
  }

  const floor = state === 'detected' ? 26 : 8;
  const ceiling = state === 'detected' ? 82 : 34;
  let phase = 0;
  const tick = () => {
    phase += 0.35 + Math.random() * 0.25;
    const wave = (Math.sin(phase) + Math.sin(phase * 2.3) * 0.5) / 1.5;
    const level = Math.max(0, Math.round(floor + ((wave + 1) / 2) * (ceiling - floor)));
    fill.style.width = level + '%';
    if (value) value.textContent = level + '%';
  };
  tick();
  if (!window.matchMedia('(prefers-reduced-motion: reduce)').matches) {
    setInterval(tick, 140);
  }
}

/* ------------------------------------------------------------------ copy */

function wireCopy() {
  document.querySelectorAll('[data-copy]').forEach((btn) => {
    btn.addEventListener('click', async () => {
      const source = document.querySelector(btn.dataset.copy);
      const note = btn.parentElement.querySelector('.copied');
      const text = source ? source.textContent.trim() : '';
      try {
        await navigator.clipboard.writeText(text);
      } catch {
        /* Mockup only: no fallback dialog, and never a native prompt. */
      }
      if (!note) return;
      note.classList.add('show');
      setTimeout(() => note.classList.remove('show'), 1600);
    });
  });
}

/* --------------------------------------------------------- opt-out gating */

function wireOptOut(state) {
  const box = document.querySelector('[data-gates-next]');
  if (!box) return;
  const next = document.querySelector('[data-nav="next"]');
  const update = () => {
    const satisfied = box.checked || matches(box.dataset.alsoReady || '', state);
    if (next) next.disabled = !satisfied;
  };
  box.addEventListener('change', update);
  update();
}

/* ------------------------------------------------------------------- boot */

document.addEventListener('DOMContentLoaded', () => {
  buildSprite();
  const state = currentState();
  applyState(state);
  wireNav();
  wireChoiceGroups(state);
  wireMeter(state);
  wireCopy();
  wireOptOut(state);
});
