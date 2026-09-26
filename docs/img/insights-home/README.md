# Home with insights captures

Every capture uses the seed log `docs/insights-mockup/seed-active.jsonl`
(`SPEECHER_INSIGHTS_SEED`) with today pinned to 2026-09-26
(`SPEECHER_INSIGHTS_TODAY`), so the numbers match the web mockup.

- `linux-*`: Qt Widgets renders offscreen with the KDE platform theme and
  Breeze style in isolated configuration directories, 1040×900 (the narrow
  one 720×900), from `e4ed5d05`.
- `mac-*`: the `insights-evidence` workflow on a macOS 26 runner at
  `e4ed5d05`. `mac-home.png` and `mac-home-bottom.png` are screen captures of
  the composited window; `mac-general-insights-off.png` is the window's
  backing store, which leaves the sidebar blank.
- `win-*`: the same workflow on a Windows Server 2025 runner at `e4ed5d05`,
  through `PrintWindow`.
