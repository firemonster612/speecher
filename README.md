# Dictation popup: before and after

Comparison captures for the popup polish PR. Nothing here is built or
shipped; the branch exists so the pull request can show pictures without
carrying binaries in its own diff.

- `popup/linux-*.png` — an offscreen Qt harness run against the PR branch
  and against its merge-base, under Breeze.
- `popup/win-*.png` — `win-panel-evidence.yml`, filming the DWM-composed
  panel window on a Windows runner.
- `popup/mac-*.png` — the mac panel's own E2E capture frames.
