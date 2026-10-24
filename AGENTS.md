## Agent skills

### Issue tracker

Issues and PRDs are tracked in this repository's GitHub Issues. See `docs/agents/issue-tracker.md`.

### Triage labels

Triage uses the five default labels. See `docs/agents/triage-labels.md`.

### Domain docs

Domain documentation uses the single-context layout. See `docs/agents/domain.md`.

## Architecture and UI changes

Speecher is a daemon, a CLI and a UI that share state through the daemon. The
CLI talks to the running daemon over the local socket
(`src/app/SingleInstanceIpc`); the Qt Quick UI runs inside the daemon process.
See `docs/adr/0009-one-quick-ui-inside-the-daemon.md`. The Android client under
`android/` follows `android/AGENTS.md` instead.

Before changing the desktop UI, read `src/frontend/quick/DESIGN.md`. Change
only what was asked: adding, removing or moving a page is the user's decision.
