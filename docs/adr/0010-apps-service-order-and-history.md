# Apps, the Service Order and History in SQLite

Status: accepted, 2026-11-08. Amends
[0006](0006-local-models-and-custom-endpoints.md): its two Custom Endpoints
are now the speech and refinement Servers, entries of a Service Order.

## Context

Three settings models had grown sideways.

- **Apps.** What Speecher did in an app was split across three lists that
  matched apps three ways: App recognition rules (substring patterns that
  set a category and a profile), Writing Profile overrides (an exact
  application id), and paste rules (an id or a category). They matched
  differently, the same app could sit in all three, and Profiles and
  Pasting each showed an approximation of the result.
- **Services.** Each job had a primary provider and up to two fallbacks,
  with per-provider fields for the refinement models, a separate language
  list for speech fallbacks, a None provider for "don't refine", and two
  Custom Endpoint structs. A Writing Profile prepended its own provider, so
  no editor showed what a dictation would actually try.
- **Insights.** A JSONL log kept counts and app names, while the last
  transcript lived only in memory. People asked to find what they had
  dictated; the log could not hold text safely or search it.

## Decision

**One app model and one matcher.** An App (`core/Apps`) is an entry the
person made, recognized by its ids (desktop file id, bundle id or program
name) or, for web apps and programs in a terminal, by text its name or
window title contains. It holds an App Type, a Writing Profile, a Pasting
choice and a History choice, each unset ("Same as …") until changed. The
App Types (Terminals, Browsers, Email, Office, Code editors, AI coding,
Other apps) carry the same choices. `resolveApp` is the only matcher: the
person's entries first (id matches before text matches), then a coding
agent detected in a terminal, the built-in catalog and the type, and it
says which layer each value came from, so every "Same as …" names the
place the run really takes it from. `migrateApps` folds the three old lists
into Apps and keeps each migrated recognition rule's substring matching as
a "name or window title contains" match, so no migrated rule changes what
it matches.

**A Service Order per job.** Speech and refinement each have an ordered
list of at most three services (`core/ServiceOrder`), first to last; each
entry carries its own model, a later speech entry its own Spoken Language,
and ChatGPT's and Claude's refinement entries their thinking and speed.
Refinement's None provider becomes the Clean up my words switch. Each job
has one Server, which replaces its Custom Endpoint. A Writing Profile names
at most one first service per job, and one resolver (`planDictation`)
settles every run over the layers (the page, the App Type, the App, the
Writing Profile, the run) and records where each value came from.
`migrateServiceOrder` writes the orders from the primary and fallback keys
and leaves the old keys readable for a release, for a downgrade.

**History in SQLite.** History (`core/history/HistoryStore`) is one SQLite
file, `history.sqlite3`, opened through Qt Sql's SQLite driver, with an FTS5
trigram index for search and an `instr` scan where the SQLite lacks the
trigram tokenizer (before 3.34). What Speecher Keeps (Nothing, Stats, Text,
Text and audio) decides what each entry holds when it is written, lowered
by the App's or App Type's History choice and always Stats for a password
field (`historyKeep`). Text and audio are deleted after their days; the
stats stay. `secure_delete` is on, the index deletes with FTS5's
`secure-delete` (merging its segments where SQLite is older than 3.42),
every permanent delete checkpoints the write-ahead log, and an Everything
clear runs `VACUUM`, so deleted text is overwritten in the file. Upgrades
import `insights.jsonl` once, marked imported in the same transaction as
its dictations, and stay at Stats, or Nothing where insights were off, until the
person turns text on; new installs keep text.

## Consequences

- The Apps page is the one place an app's profile, pasting and History
  choice are set. Pasting and General's Privacy & data list the apps that
  differ, read-only, each opening in Apps.
- Services and their sign-ins live once, on Services; elsewhere a service
  that can't run shows its status and a Fix sheet with the same card.
- History needs the Qt Sql SQLite plugin packaged on every platform; the
  packaging checks open a History to catch a missing one before release.
- A release after these ship, the retired keys and the read-only migration
  inputs (`insights.jsonl.imported`, `insights/enabled`, the old provider
  and rule keys) can be deleted.
- Matching by text is broader than by id. The picker creates id matches;
  only "Match by name or window title" and migrated rules create text ones.
