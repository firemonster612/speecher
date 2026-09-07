Four setup changes, all four driven end to end on Linux.

## Installing the AppImage is now a real, required step

The Global Shortcut page's button (previously "Add Speecher to your app menu") is now "Install Speecher". Clicking it moves the AppImage into `~/Applications` (or an existing `~/AppImages` folder, which is left alone if the image already lives there), then writes the app menu entry, icon, and the `~/.local/bin/speecher` command link against the new location. `APPIMAGE` is updated in-process and the updater re-reads it, so update checks, installs, and restarts follow the moved file.

The step is required: Next stays disabled on that page until the install has run, and "Skip setup" is hidden for the whole run while the install is pending, since skipping used to record setup as completed with the image still in Downloads. Cancel remains available and completes nothing. A command link left behind by an older version pointing at a non-installed location does not satisfy the gate; clicking Install repairs and moves everything.

Both wizard variants are gated: `KAssistantDialog::setValid` in the KDE build, a `QWizardPage::isComplete` override otherwise.

## Enabling the virtual keyboard is an explicit step

The old verify dialog had only "Run test" and Cancel, and a passed test enabled ydotool as a side effect. The dialog now has an explicit Enable button that stays locked until a typing test passes; only clicking Enable turns virtual keyboard paste on. This covers both entry points into the flow, the setup assistant's Text delivery page and the Output settings row. The needs-sign-out path no longer enables ydotool behind the user's back either; the sign-in notes now direct the user to the Enable step in the Output settings after they sign back in. ydotool is Linux-only, so macOS and Windows have no such installer step.

Locked until the test passes:

![ydotool enable locked](https://raw.githubusercontent.com/firemonster612/speecher/evidence/pr__NUM__-linux-setup/ydotool-enable-locked.png)

Unlocked after a passed test; enabling still takes the click:

![ydotool enable unlocked](https://raw.githubusercontent.com/firemonster612/speecher/evidence/pr__NUM__-linux-setup/ydotool-enable-unlocked.png)

## Provider stats lead with a score out of 10

Both the speech and refinement provider stat blocks start with a Score line: Claude Voice 8/10, ChatGPT Codex 9/10, OpenAI refinement 9/10, Anthropic refinement 8/10, ranking the existing measured speed and quality lines. The macOS and Windows setup flows render the same descriptor stats, so they pick the line up without platform changes.

![transcription page with score](https://raw.githubusercontent.com/firemonster612/speecher/evidence/pr__NUM__-linux-setup/setup-transcription.png)

![refinement page with score](https://raw.githubusercontent.com/firemonster612/speecher/evidence/pr__NUM__-linux-setup/setup-refinement.png)

## The gate, before and after

Before installing: Next disabled, Skip setup gone, the manual command quoting the Downloads path.

![global shortcut page before install](https://raw.githubusercontent.com/firemonster612/speecher/evidence/pr__NUM__-linux-setup/setup-global-shortcut.png)

After installing: button reads Installed, Next and Skip return, the command now goes through `~/.local/bin/speecher`.

![global shortcut page after install](https://raw.githubusercontent.com/firemonster612/speecher/evidence/pr__NUM__-linux-setup/setup-global-shortcut-installed.png)

## Verification

- Unit suite green (`ctest`, 20/21; `speecher_linux_style_choice` fails for a pre-existing environmental reason: the system breeze6.so was rebuilt against Qt 6.10 while the local build uses Qt 6.8.3). New tests: AppImage relocation including the `~/AppImages` preference, same-name replacement, and the `~/Applications`-as-file edge; the enable dialog's locked-until-test-passes gating.
- New committed E2E rig at `tests/e2e/linux-setup` (same pattern as `linux-update`): builds the AppImage, runs it on a fresh profile under kwin_wayland in bwrap, and drives the whole assistant over AT-SPI. Final run passed all ten assertions: score lines on both provider pages, Skip hidden while the install is pending, Next held, the move into `~/Applications`, command link, menu entry and icon present, Skip and Next returning, and setup completing only after the install.
- Two independent review rounds (one Claude reviewer, one GPT reviewer per round). Round 1 findings (Skip bypass, needs-sign-out auto-enable, legacy-symlink gate, destructive cross-filesystem fallback, `~/Applications`-as-file, stale `APPIMAGE` cache) are all fixed and re-verified in round 2.

Known limits: the KDE `KAssistantDialog` variant is verified by reading the KF6 sources, not compiled locally (the host's KF6 packages are Qt 6.10, the local Qt is 6.8.3); CI's KDE build covers compilation. Declined edge: a desktop file deleted by hand while the command link stays intact still counts as installed, since the installer writes the link last and only manual tampering produces that state.

🤖 Generated with [Claude Code](https://claude.com/claude-code)
