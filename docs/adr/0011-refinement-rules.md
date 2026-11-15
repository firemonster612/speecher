# Refinement rules

Status: accepted, 2026-11-15. Amends
[0008](0008-phone-transfer-over-the-lan.md): the bundle is format 2.

## Context

Refinement took the person's own prose from four places: Additional
Instructions for every profile, each Writing Profile's Instructions box,
custom tones and custom cleanup levels. All four were a named instruction
with a scope, but each had its own form, its own storage and its own place
in the prompt. A custom level could also replace step 3 of the prompt with
the person's text ("Custom only"), which no benchmark had measured.

The four built-in cleanup levels were opaque. Keeping filler words at
Medium, or dropping Markdown, meant writing a custom level that argued with
the level's own text.

## Decision

**One list of rules, switched per profile.**

- One of the person's rules (`RefinementRule` in `core/Target.h`) has an
  id, a name, an optional instruction and `everyProfile`. Refinement is told
  the instruction, or the name when it is blank. A rule is off unless a
  profile turns it on, or, with `everyProfile`, on unless one turns it off.
- The built-in rules are one table in `core/RefinementRules`: an id, the
  editor group, the lowest level that starts it on, and the sentence the
  prompt adds when a profile switches it on or off against that level.

  | Group | Rules | Start on at |
  | --- | --- | --- |
  | cleanup | `fillers`, `smoothing`, `structure` | Medium |
  | cleanup | `rewrite`, `headings` | High |
  | formatting | `digits`, `markdown`, `noEmDashes` | Light |
  | more | `britishSpelling`, `lowercase` | never |

  `codingAgent` (more) starts on in the built-in AI coding profile at any
  level and off elsewhere, and adds the coding-agent section rather than a
  sentence. None starts what Light does, the level it translates at. So a
  rule that starts at Light has no "on" sentence, and `britishSpelling` and
  `lowercase` have no "off" sentence. Labels and help are QML wording keyed
  by id (`Library.ruleWords`), and a test checks every id in core is worded.
- A profile stores only the switches that differ from the default
  (`WritingProfileSettings::rules`, id to bool), and `ruleOn` resolves the
  rest. Choosing a level, in the editor or with a run's `--cleanup`, drops
  the profile's built-in switches and keeps its switches for the person's
  rules.
- The Cleanup Level stays a stored setting (None, Light, Medium, High) and
  the Tone a choice of built-in tones.
- A custom system prompt replaces the built-in rules. The Tone and the
  person's rules still apply.

**At Light, Medium and High with no built-in rule changed, the prompt stays
byte for byte the same.** The level paragraphs, the format step and every
other built-in section are the text they were before rules. A changed
built-in rule does not rewrite step 3. It adds its sentence to a section
after step 5 and before the examples, so the model reads the tuned level,
then its exceptions:

```
The user's changes to the steps above
- Keep filler words, stutters, repeated words and false starts as spoken.
- Write plain text with no Markdown.
```

The person's rules that are on go under "The user's own instructions", with
its two-line preamble, in their order, joined by a blank line. Selection
editing and a custom system prompt get no changed steps: neither has the
steps they change.

Why: the Quality ratings in `tools/refinement-benchmark` were measured on
that text. Changing it would leave the ratings describing a prompt the app
no longer sends, and step 3 composed from sentence fragments has never been
measured.

How it is enforced: the prompt goldens in
`android/protocol/src/test/resources/refinement-prompt/`, which
`tests/test_refiners.cpp` and the Android protocol tests both compare.
`built-in`, `balanced-instructions`, `balanced-messages`, `light-unknown`
and `strong-chatgpt` did not change in this work, and
`tests/data/refinement-prompt-compact/` pins the Local Runner's compact
prompt at each level. `SPEECHER_RECORD_GOLDENS=1` rewrites them; a diff to
any of those files is a prompt change, which needs the benchmark run again.

**Migration.** `migrateRules` (`core/settings/RulesMigration`, marker
`migration/rulesVersion`) makes rules, in this order, so the person's text
reaches the prompt in the order it did:

1. Non-blank global instructions: "Instructions for every profile", on in
   every profile.
2. Each profile's non-blank instructions: "<profile> instructions", on in
   that profile.
3. Each custom tone: "<name> tone", telling refinement to write in it, on in
   each profile that chose it, which then has no tone.
4. Each custom cleanup level: its name and instructions, on in each profile
   that chose it, which then has the level's base, or Light for Custom only.

A name already taken gets " 2", " 3" and so on.
`refinement/additionalInstructions`, `refinement/customTones` and
`refinement/customCleanupLevels` are never written again and stay in the
file for a downgrade to read.

**Phone transfer.** The bundle is format 2: profiles carry their switches
and the person's rules travel as one list, merged by id with the computer's
version replacing the phone's, as tones were. ADR 0008 has the shape.

## Consequences

- Global and profile instructions migrate to the same prompt they made
  before. Custom tones and levels do not: their text moves from step 3 to
  the person's rules, and a Custom only level becomes Light plus a rule.
- A downgrade sees the retired keys as they were at migration. Rules made
  since don't reach it.
- A phone and a computer must both be on format 2 to transfer.
- A new built-in rule needs its sentences, its QML wording and, if it
  starts on at some level, a level whose text already does it: the sentence
  is only ever an exception to that text.
- No benchmark measures a changed step. Whether a model follows one is
  checked by hand.
- Not done: a `--rule` flag or run layer, built-in rules in selection
  editing, and reordering the person's rules.
