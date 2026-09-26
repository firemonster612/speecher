# Home with insights captures

Every capture uses the seed log `docs/insights-mockup/seed-active.jsonl`
(`SPEECHER_INSIGHTS_SEED`) with today pinned to 2026-09-26
(`SPEECHER_INSIGHTS_TODAY`), so the numbers match the web mockup.

- `linux-*`: Qt Widgets renders offscreen with the KDE platform theme and
  Breeze style in isolated configuration directories, 1040×900 (the narrow
  one 720×900), from `e4ed5d05`.
- `mac-*`: the `insights-evidence` workflow on a macOS 26 runner at
  `eb803ca6`. `mac-home.png` and `mac-home-bottom.png` are screen captures of
  the composited window; `mac-general-insights-off.png` is the window's
  backing store, which leaves the sidebar blank.
- `win-*`: the same workflow on a Windows Server 2025 runner at `e6399c02`,
  through `PrintWindow`.

## Profile badge and chart hover

- `badge-linux-dark.png`, `badge-linux-light.png`: Where your words go with
  the Writing Profile badges, from the same Linux rig at `eb803ca6`.
- `badge-windows.png`, `badge-macos.png`: cropped from the `insights-evidence`
  run at `eb803ca6` (Windows `win-home-middle.png`, macOS
  `mac-home-bottom-screen.png`).
- `hover-linux-dark.png`, `hover-linux-light.png`: the year heatmap and the hour
  chart rendered offscreen with a day and an hour hovered, in Breeze Dark and
  Light. CI captures pages without a pointer, so hover has no Windows or macOS
  capture.
