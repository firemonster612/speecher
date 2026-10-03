# Stats image on every platform

What Home's "Copy image with stats" puts on the clipboard, read back from the
clipboard by `SPEECHER_GRAB_PAGE=stats-image` in the Insights evidence
workflow. Today is pinned to 2026-09-26 and the period is Last 30 days.

- `<platform>-stats-image.png`: the insights mockup seed
  (`docs/insights-mockup/seed-active.jsonl`).
- `<platform>-stats-image-large.png`: `docs/insights-mockup/seed-large.jsonl`,
  30 million words a dictation, so the period holds 2.7 billion words, past
  what an int holds.
- `<platform>-stats-image-dark.png`: the mockup seed in the dark theme: the
  Breeze Dark colour scheme on Linux, the Theme setting set to Dark on macOS
  and Windows.
- `windows-pasted-large.jpg`: the large seed copied from the Share menu in a
  Windows 11 session and read from the clipboard by another process, scaled
  to JPEG for size.

Linux is rendered offscreen with the KDE platform theme and Breeze; macOS and
Windows on the GitHub runners.
