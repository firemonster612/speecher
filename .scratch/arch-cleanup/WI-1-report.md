# WI-1 SettingsDialog page split report

## Completed staged commits

- `84e7be6` — extracted `SettingsPageSupport.{h,cpp}`, including row/card/separator/combo/section helpers and KDE/fallback page-container handling.
- `1c72d2f` — extracted `GeneralSettingsPage.{h,cpp}` with its widgets, wiring, `load`, `appendToDraft`, `hasChanges`, and `changed` signal.

## Current state

- Extracted pages: General.
- `SettingsDialog.cpp` line count: 2176.
- Working tree was clean after the General page commit.
- Function inventory: no removals versus `.scratch/arch-cleanup/functions-baseline.txt` (the current branch additionally contains five startup-preparation-runner tests).

## Verification

Gate after `SettingsPageSupport`:

```text
1/1 Test #1: speecher_tests ...................   Passed   14.84 sec
100% tests passed, 0 tests failed out of 1
Total Test time (real) = 14.84 sec
```

Gate after `GeneralSettingsPage`:

```text
1/1 Test #1: speecher_tests ...................   Passed   14.72 sec
100% tests passed, 0 tests failed out of 1
Total Test time (real) = 14.72 sec
```

The sandboxed baseline CTest run timed out while Qt Multimedia attempted to connect to PulseAudio (`Operation not permitted`). All recorded gates were therefore run with desktop audio access outside that sandbox restriction.

## Not completed

Vocabulary, Corrections, Bindings, Output, Refinement, Provider, and Audio were not attempted in this run. They were not determined to be behavior-identically infeasible and should not be treated as skipped on architectural grounds. The final coordinator cleanup was not performed because those page extractions remain.

- Extracted `VocabularySettingsPage` in `refactor(ui): extract VocabularySettingsPage`.
- `SettingsDialog.cpp` line count: 1951.
- Extracted `CorrectionsSettingsPage` in `refactor(ui): extract CorrectionsSettingsPage`.
- `SettingsDialog.cpp` line count: 1813.
- Extracted `BindingsSettingsPage` in `refactor(ui): extract BindingsSettingsPage`.
- `SettingsDialog.cpp` line count: 1531.
- Extracted `OutputSettingsPage` in `refactor(ui): extract OutputSettingsPage`.
- `SettingsDialog.cpp` line count: 980.
- Extracted `RefinementSettingsPage` in `refactor(ui): extract RefinementSettingsPage`.
- `SettingsDialog.cpp` line count: 698.
- Extracted `ProviderSettingsPage` in `refactor(ui): extract ProviderSettingsPage`.
- `SettingsDialog.cpp` line count: 426.
