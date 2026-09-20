# Speecher setup wizard — web mockups

Static mockups of the first-launch setup wizard, meant as the design source of
truth for the Linux Qt, Windows and macOS implementations.

Open any page directly from disk (`file://`) — there is no build step, no
framework, no network access. All nine pages share `wizard.css` and
`wizard.js`.

## Reading a state

Every page takes an optional `?state=` query parameter. With no parameter the
page renders its default state (listed first in each table below). An
unrecognised value falls back to the default.

Example: `transcription.html?state=notready`

## Pages and states

| # | File | State | What it shows |
|---|------|-------|---------------|
| 1 | `welcome.html` | `nocreds` *(default)* | Neither sign-in found; both hint lines visible; Next disabled |
| | | `checking` | Both rows spinning on "Checking…"; Check again disabled; Next disabled |
| | | `found` | ChatGPT sign-in found, Claude Code still missing with its hint; Next enabled |
| 2 | `transcription.html` | `ready` *(default)* | ChatGPT Codex selected and Ready; stats table; Next enabled |
| | | `notready` | Selected provider not signed in; status line, sign-in hint, Check again; Next disabled |
| 3 | `microphone.html` | `listening` *(default)* | Level bar moving at a low level; "Listening for microphone input…"; Next disabled |
| | | `detected` | Level bar moving strongly; "Microphone input detected."; Next enabled |
| | | `silent` | Flat bar at 0%; escalated hint about mute/other device; Check again; Next disabled |
| | | `nomic` | Device dropdown disabled and empty; "No microphone was found."; Next disabled |
| 4 | `accessibility.html` | `off` *(default)* | Both capabilities blocked, warning notice, "Enable permanently"; Next disabled |
| | | `on` | Both allowed, confirmation notice, no button; Next enabled |
| 5 | `delivery.html` | `notinstalled` *(default)* | "Not installed…", Set up button; Next disabled until the opt-out is checked |
| | | `ready` | "Virtual keyboard ready."; Next enabled |
| | | `signout` | Log-out-and-back-in instruction, Set up button still offered; Next enabled |
| 6 | `refinement.html` | `ready` *(default)* | OpenAI selected and Ready; stats; Fast mode |
| | | `notready` | OpenAI selected but not signed in; warning that dictation delivers the raw transcript |
| | | `none` | None selected; no stats table, no Fast mode |
| 7 | `profiles.html` | *(single state)* | Default-profile dropdown and the Cleanup/Tone grid |
| 8 | `shortcut.html` | `captured` *(default)* | F13 set, Set shortcut button, full behaviour dropdown |
| | | `manual` | Desktop cannot register a shortcut; copyable command; behaviour locked to Toggle with the hold-to-talk note |
| 9 | `ready.html` | `complete` *(default)* | "Setup is complete.", the activation instruction, per-step Ready list; Finish enabled |
| | | `blocked` | Checklist of unfinished steps, each with a reason and a "Go to step" button; Finish disabled |

Refinement (6) and Writing profiles (7) are never gated — Next is always
enabled. Every other gated page states, on screen, why Next is unavailable;
`ready.html?state=blocked` never leaves Finish silently disabled.

## Screenshotting every state

```sh
for p in welcome.html?state=nocreds welcome.html?state=checking \
         welcome.html?state=found transcription.html?state=ready \
         transcription.html?state=notready microphone.html?state=listening \
         microphone.html?state=detected microphone.html?state=silent \
         microphone.html?state=nomic accessibility.html?state=off \
         accessibility.html?state=on delivery.html?state=notinstalled \
         delivery.html?state=ready delivery.html?state=signout \
         refinement.html?state=ready refinement.html?state=notready \
         refinement.html?state=none profiles.html \
         shortcut.html?state=captured shortcut.html?state=manual \
         ready.html?state=complete ready.html?state=blocked; do
  out=$(echo "$p" | sed 's/[?=.]/_/g')
  chromium --headless --disable-gpu --hide-scrollbars \
    --window-size=800,660 --screenshot="$out.png" "file://$PWD/$p"
done
```

## Design rules these files encode

- **Dialog, not page.** 720x560 fixed, centred on a plain neutral backdrop,
  title row on top, footer row at the bottom. The body scrolls; the frame
  never grows.
- **Footer order.** "Skip setup" as a link on the left. Back, Next (primary),
  Cancel on the right, in that order. The last page uses Finish instead of
  Next and drops the Skip link entirely, because there is nothing left to
  skip past it.
- **One accent.** A single blue is used for the primary button, the selected
  provider card and focus rings. Green, amber and red appear only in status
  text and notices. Everything else is neutral grey.
- **No web flair.** No gradients, no glass, no pill buttons, no shadows except
  the one that lifts the dialog off the backdrop, no emoji.
- **Everything wraps.** Both checkbox labels on `delivery.html`, the long
  status lines and the hint lines all wrap inside the dialog width; nothing
  is truncated or ellipsised at 720px.
- **Gating is explained.** A disabled Next is always accompanied by a visible
  reason and, where possible, the action that fixes it.

## Icons

All SVG geometry lives in one place: the `ICONS` map at the top of
`wizard.js`, injected once per page as a hidden `<symbol>` sprite and
referenced with `<use href="#icon-name">`. The line icons are drawn on a 24x24
grid in `currentColor`, so a native port can copy the path data directly. An
`ICONS` entry is either a body string, which assumes that 24x24 grid, or
`{viewBox, body}` for a mark that needs its own coordinate space.

| Symbol | Used for |
|--------|----------|
| `icon-openai` | ChatGPT / Codex / OpenAI rows and cards. The real ChatGPT mark, copied from `assets/brand/chatgpt.svg` (viewBox `0 0 2406 2406`), in its own colours: a `#74aa9c` rounded tile with the white knot on top. The knot cell is one path stamped six times by `<use>`; its id is namespaced `chatgpt-cell` because the sprite is shared. |
| `icon-anthropic` | Claude / Claude Code / Anthropic rows and cards. The real Claude starburst, copied from `assets/brand/claude.svg`, in the brand `#D97757` rather than `currentColor`. |
| `icon-mic` | Microphone device row, ready checklist |
| `icon-keyboard` | Virtual keyboard, context reading, ready checklist |
| `icon-cursor` | Pasting into the focused app |
| `icon-check` | Ready / found / detected statuses |
| `icon-checkCircle` | Spare, for a heavier confirmation mark |
| `icon-warning` | Warnings and blocked-step rows |
| `icon-pending` | Not found / not installed / None |
| `icon-spinner` | "Checking…" (rotates; static under `prefers-reduced-motion`) |
| `icon-copy` | Copy button on the manual shortcut command |
| `icon-arrow` | Spare, for a trailing "go to" affordance |

Both brand marks are the real trademarks, not alikes, so the shipping app has
to be permitted to use them. They keep their own colours because that is what
carries the recognition at 20px; a flat `currentColor` silhouette of either
one loses it.

## Deliberate simplifications

- Cancel, Skip setup and the action buttons (Check again, Enable permanently,
  Set up virtual keyboard, Set shortcut) are inert. Back and Next do navigate,
  so the nine pages can be clicked through in order.
- Copy on `shortcut.html` really does write to the clipboard and shows an
  inline "Copied" confirmation — no native dialog, per the no-browser-prompts
  rule.
- The microphone level bar is driven by a small random walk in `wizard.js`;
  it stops entirely under `prefers-reduced-motion`.
- Provider names, statuses, stats and hint strings match the current
  implementation. `accessibility.html` intentionally uses the fuller,
  more honest copy from the brief rather than today's shipped string, which
  mentions only pasting.
