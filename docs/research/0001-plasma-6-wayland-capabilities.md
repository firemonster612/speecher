# Plasma 6 Wayland target and delivery capabilities

Status: decision report, 2026-07-22

## Decision

Speecher can support press-and-release activation on Plasma 6 Wayland and can offer useful Target context in many applications. It cannot promise the same context, insertion, verification, or edit-observation behavior everywhere. Wayland deliberately provides no public client API for reading every window, choosing an arbitrary foreign input target, or learning what another application did with a synthetic key event.

Ship capability profiles and report the profile used for each Dictation Session:

| Profile | What Speecher may promise | Main mechanism |
| --- | --- | --- |
| Copy | The Refined Transcript was placed on the clipboard | `wl-copy` or Qt clipboard |
| Focus-bound paste | Speecher requested a Paste Rule at the control that still has focus | Global Shortcuts plus ydotool or the Remote Desktop portal |
| Accessible Target | Speecher captured a Target, bounded text context, caret and selection when exposed, and can verify some edits | AT-SPI, with synthetic paste or guarded `EditableText` calls |
| IDE-native | Speecher knows editor, document, language, selections, and edit result according to the IDE | An installed IDE plugin |

The safe fallback is Copy. Never describe a successful ydotool or portal input call as verified insertion. If focus or Target identity changes before delivery, do not send synthetic paste unless the user has explicitly chosen a current-focus Paste Rule. A saved, still-valid AT-SPI editable object may be changed directly without focus, but only after Speecher checks that its caret, selection, and nearby-text fingerprint still match the start snapshot.

Use the XDG Global Shortcuts portal as the preferred activation API. Keep KF6 `KGlobalAccel` as a native Plasma option when a portal session is unavailable. Both expose activation and deactivation, which maps cleanly to push-to-talk. Add AT-SPI as an optional context and receipt provider, starting read-only. Keep ydotool as an opt-in compatibility backend. Evaluate the keyboard-only XDG Remote Desktop portal as the no-`uinput` input backend. IDE plugins belong above AT-SPI, not behind an attempt to infer code semantics from accessible text.

## What is publicly supported

### Press and release

The stable [`org.freedesktop.portal.GlobalShortcuts`](https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.portal.GlobalShortcuts.html) interface has separate `Activated` and `Deactivated` signals with timestamps. Its shortcuts work regardless of application focus. Binding normally presents a user-facing chooser, and the session belongs to the requesting application.

Plasma's portal backend implements Global Shortcuts version 2 and forwards KGlobalAccel's pressed and released signals to those two portal signals. See KDE's current [`globalshortcuts.cpp`](https://invent.kde.org/plasma/xdg-desktop-portal-kde/-/blob/449b1aeef0d44f4c74a500151bc092a228e3b833/src/globalshortcuts.cpp) and [`kde.portal`](https://invent.kde.org/plasma/xdg-desktop-portal-kde/-/blob/449b1aeef0d44f4c74a500151bc092a228e3b833/data/kde.portal). The KDE backend currently emits empty options, so Speecher must not depend on an `activation_token` being present.

KF6 [`KGlobalAccel`](https://api.kde.org/kglobalaccel.html) is also a public Plasma API. Its `globalShortcutActiveChanged(QAction *, bool)` signal documents press as active and release as inactive. It needs a stable component and action ID, and Plasma stores the user's assignment. This is suitable for a host-installed build but ties the activation layer to KDE Frameworks.

The current System Settings command shortcut invokes `speecher toggle` once. Speecher's CLI forwards that one command or starts a daemon ([`src/app/main.cpp:84`](../../src/app/main.cpp)). It has no release channel, so it can remain a toggle fallback but cannot implement push-to-talk. Press-and-release registration needs to live in the long-running Speecher process.

Release handling must be defensive. Cancel capture if the shortcut session closes, the portal disappears, the screen locks, or a maximum hold timer expires without `Deactivated`. Ignore duplicate press signals until the matching release.

### Focus-preserving overlay

Speecher already asks Qt not to activate its popup and, when LayerShellQt is present, sets keyboard interactivity to none and `activateOnShow` to false ([`src/ui/WaylandLayerShell.cpp:19`](../../src/ui/WaylandLayerShell.cpp)). That is the right base. It lets the original editable control retain focus while the overlay is visible.

This is a behavior to test, not a Target identity. Another window can take focus during transcription, the original control can disappear, or a modal can open. Speecher still needs a start snapshot and a delivery-time comparison.

### App, focused control, caret, selection, and nearby text

AT-SPI is the public Linux accessibility interface for this job. Qt's Linux accessibility bridge uses AT-SPI, and Qt widgets expose an accessibility tree to clients. Qt documents the bridge conditions and the `org.a11y.Status` settings in [`QAccessible`](https://doc.qt.io/qt-6/qaccessible.html). KDE also uses AT-SPI to automate Qt 5 and Qt 6 applications under KWin in its [Appium guidance](https://develop.kde.org/docs/apps/tests/appium/).

At shortcut press, an AT-SPI client can find the focused accessible, retain its object reference, walk to its application and window ancestors, and record its PID, role, name, toolkit, and states. [`AtspiAccessible`](https://gnome.pages.gitlab.gnome.org/at-spi2-core/libatspi/class.Accessible.html) exposes application, process, interface, role, state, and tree queries.

If the focused object implements [`AtspiText`](https://gnome.pages.gitlab.gnome.org/at-spi2-core/libatspi/iface.Text.html), Speecher can read:

- the caret offset;
- all reported text selections;
- a bounded character range around the caret;
- line, word, or other granularity ranges;
- text attributes and on-screen character bounds when the application supplies them.

Only request a small window around the caret. Do not read or retain a whole document just because the interface permits it. The Target snapshot should hold the minimum needed for the Writing Profile, stale-target check, and later verification.

AT-SPI does not guarantee that an application exposes the control. Custom canvases, terminals, games, remote desktops, and some web or Electron configurations may expose only a shallow tree. Even good implementations vary in text boundaries, selection support, and event detail. Qt itself notes that custom widgets must implement accessibility interfaces and send events ([Qt QWidget accessibility](https://doc.qt.io/qt-6/accessible-qwidget.html)).

AT-SPI yields application and control identity, but it is not always a canonical desktop-file ID. A user-installed KWin script may supplement it with `workspace.activeWindow` fields such as `desktopFileName`, `resourceClass`, PID, caption, and `internalId`. These are documented in the [KWin scripting API](https://develop.kde.org/docs/plasma/kwin/api/), and scripts can call Speecher over D-Bus. Treat this as a KDE-only helper that the user installs and enables, not as a base dependency. It still cannot supply caret or document text.

### Insertion

There are three supported routes, each with a different guarantee.

AT-SPI [`EditableText`](https://gnome.pages.gitlab.gnome.org/at-spi2-core/libatspi/iface.EditableText.html) can insert or delete text and can request clipboard paste at a character position. Calls address the saved accessible object rather than whatever now has keyboard focus. This is the only general desktop API found that can sometimes insert after focus has changed.

But support is per control. Replacing a selection with separate delete and insert calls is not atomic, applications may produce different undo history, and a stale accessible can fail or point at changed content. Prefer a normal paste while the same Target remains focused. Use direct AT-SPI mutation only for a tested application/control combination, after checking object liveness and the start fingerprint. A false or timed-out call falls back to Copy, never blind synthetic input.

For keyboard emulation without elevated `uinput` access, the XDG [`RemoteDesktop`](https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.portal.RemoteDesktop.html) portal can grant a keyboard device and accept keycodes/keysyms or a libei sender connection. Plasma's portal advertises and implements Remote Desktop version 2 in [`remotedesktop.h`](https://invent.kde.org/plasma/xdg-desktop-portal-kde/-/blob/449b1aeef0d44f4c74a500151bc092a228e3b833/src/remotedesktop.h). Request keyboard only, without screen content. The user must approve the session, and a persistent session may use a one-use restore token. This is a supported route, but its consent dialog and ongoing-session UX need live testing before it becomes the default.

ydotool remains workable. Its own [documentation](https://github.com/ReimuNotMoe/ydotool) says `ydotoold` writes a kernel `uinput` virtual device and needs access to `/dev/uinput`. Speecher's setup narrows that permission to its user daemon, but the resulting device is still system-wide. It sends input to the current focus and has no Target parameter.

Paste shortcuts vary by application. Speecher currently emits Linux keycodes for `Ctrl+Shift+V` on every ydotool paste ([`src/output/YdotoolDelivery.cpp:164`](../../src/output/YdotoolDelivery.cpp)). That is usual in terminals, but many editors and form fields use `Ctrl+V`, and some applications assign `Ctrl+Shift+V` to paste-special behavior. A Paste Rule must select the shortcut or direct-edit route by application/control category.

### Verification and brief edit observation

AT-SPI supplies the pieces for a qualified receipt. After insertion, wait for an `object:text-changed` or `object:text-caret-moved` event from the saved accessible, then read the bounded range back and compare it with the expected edit. [`AtspiEventListener`](https://gnome.pages.gitlab.gnome.org/at-spi2-core/libatspi/method.EventListener.register.html) lists text-changed, selection-changed, caret, focus, and window events. `register_with_app` can scope delivery to one application, and Speecher can reject callback sources other than the saved object.

Call the result verified only when readback matches the intended edit, allowing for a known application's line-ending or autoformat behavior. An AT-SPI mutation method returning success is accepted, not verified. A Remote Desktop or ydotool command exiting zero is input-sent, not verified. Clipboard ownership or readback proves only Copy.

For a Learned Correction, keep the verified inserted span plus a small surrounding fingerprint in memory, listen briefly to text events from that same Target, and stop on focus loss, Target destruction, timeout, or an ambiguous edit. Compare readback rather than trusting event payloads. Do not persist raw surrounding text. Ask for an explicit user setting before observing edits, since the accessibility bus has no per-field consent dialog.

This mechanism will miss edits when the app does not emit usable events, replaces its accessible object, or hides text. It can also see autoformatting and programmatic edits. Those cases are not safe to learn from without an IDE plugin or a user confirmation step.

### IDE context

AT-SPI can expose an editor's visible text, caret, selection, labels, and sometimes document attributes. It does not define project roots, language IDs, syntax nodes, diagnostics, multiple editor groups, or versioned edit transactions.

Use opt-in IDE plugins for that tier:

- The [VS Code extension API](https://code.visualstudio.com/api/references/vscode-api) exposes `window.activeTextEditor`, document text and URI, language ID, selections, change events, and transactional `TextEditor.edit` replacements.
- A Kate plugin can use [`KTextEditor::View`](https://api.kde.org/ktexteditor-view.html) for cursor and selection and [`KTextEditor::Document`](https://api.kde.org/ktexteditor-document.html) for document text and changes.
- IntelliJ plugins can map an editor document to a project-scoped `PsiFile`, as described in the [IntelliJ PSI guide](https://plugins.jetbrains.com/docs/intellij/psi-files.html).

Keep plugin IPC local, authenticate the peer, version the messages, and send bounded context unless the user asks for more. The plugin should perform the edit and return its own document version or change receipt. That preserves undo semantics and handles focus changes far better than keyboard emulation.

## Private, brittle, or unsuitable hooks

Do not bind `org_kde_plasma_window_management` from Speecher. Its protocol text calls it a desktop implementation detail, warns regular clients not to use it, permits incompatible changes, and allows only one client to bind. See KDE's [`plasma-window-management.xml`](https://invent.kde.org/libraries/plasma-wayland-protocols/-/blob/master/src/protocols/plasma-window-management.xml).

Do not call KWin's fake-input or internal EIS D-Bus objects directly. Plasma grants its portal backend special Wayland interfaces in the backend desktop file. The public security and consent boundary is the Remote Desktop portal.

The Wayland virtual-keyboard and input-method protocols are not a normal application entitlement. Compositors may reject untrusted clients, and input-method protocols are for the configured input method. Direct use would either fail on stock Plasma or depend on privileged compositor configuration.

KWin's packaged scripting API is supported for user-installed window-manager helpers. Loading ad hoc scripts through undocumented D-Bus details, scraping KWin debug support text, or depending on current object paths would be brittle. If the optional helper is built, package it and require the user to enable it through the documented KWin script flow.

XDG activation tokens do not solve Target restoration. The [`xdg-activation-v1`](https://gitlab.freedesktop.org/wayland/wayland-protocols/-/blob/main/staging/xdg-activation/xdg-activation-v1.xml) token lets a client ask the compositor to activate a surface, subject to focus-stealing rules. It does not identify the previously focused foreign surface, and an ineffective token has no validity query.

The XDG Input Capture portal is also the wrong direction. Its [specification](https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.portal.InputCapture.html) captures physical input after compositor-controlled pointer-barrier activation and says applications cannot activate capture immediately. It is not an insertion API.

## Hard limits and security policy

No supported API can prove paste into an arbitrary Wayland application when that application offers no readable accessibility or plugin interface. No supported API can direct ydotool or portal virtual-keyboard events to a saved window. No public Wayland API gives an ordinary client a trustworthy list of all windows and their focused controls.

Secure fields are a deny case. Refuse context collection, direct mutation, verification, and edit observation when AT-SPI reports `ATSPI_ROLE_PASSWORD_TEXT` or an equivalent password state. Qt exposes a `passwordEdit` accessibility state ([`QAccessible::State`](https://doc.qt.io/qt-6.8/qaccessible-state.html)), but custom controls may fail to mark themselves. A terminal password prompt is harder: a generic client cannot reliably tell that terminal echo is off or that `sudo`, SSH, or another program is reading a secret. Terminal context and learning therefore require a conservative per-application setting and must never be advertised as secure-field detection.

Clipboard delivery also has a privacy cost. Clipboard managers may retain the transcript even if Speecher restores the prior clipboard. Restoration is convenience, not erasure.

AT-SPI access is broad. For Qt applications, exposure normally depends on the session accessibility status flags, or on `QT_LINUX_ACCESSIBILITY_ALWAYS_ON`. A host application also needs access to the session accessibility bus. There is no standard per-Target approval prompt comparable to a portal. Make context capture and Learned Corrections separate user choices, bound context size, show the active profile, and discard session context promptly.

## Implications for the current code

The current output layer has no Target concept. `PlatformIntegration` reports one Linux output stack ([`src/platform/PlatformIntegration.cpp:74`](../../src/platform/PlatformIntegration.cpp)), and `DictationSession::deliverFinal` passes only settings and text to `TextDelivery` ([`src/dictation/DictationSession.cpp:458`](../../src/dictation/DictationSession.cpp)). A later design needs separate activation, Target snapshot, insertion, and receipt interfaces rather than more booleans on `TextDelivery`.

Current ydotool success means the subprocess exited zero. Duplicate suppression also returns success without sending another input sequence ([`src/output/YdotoolDelivery.cpp:221`](../../src/output/YdotoolDelivery.cpp)). Neither is an insertion receipt.

Clipboard restoration waits a fixed 250 ms after ydotool returns ([`src/output/TextDelivery.cpp:24`](../../src/output/TextDelivery.cpp), [`src/output/TextDelivery.cpp:178`](../../src/output/TextDelivery.cpp)). Wayland clipboard transfer is request-driven, so a slow Target may request data after restoration. Verification should drive restore timing where possible, with a bounded timeout and a clear unverified result otherwise.

The automatic backend order is ydotool, `wl-copy`, then Qt clipboard, and its unit tests confirm only selection order and generated keycodes ([`src/output/TextDelivery.cpp:270`](../../src/output/TextDelivery.cpp), [`tests/test_core.cpp:1082`](../../tests/test_core.cpp), [`tests/test_core.cpp:1238`](../../tests/test_core.cpp)). Clipboard fallback is already labelled `Copied`, which should remain distinct from requested or verified insertion.

Suggested receipt states are `Copied`, `Input sent`, `Accepted by Target`, and `Verified in Target`. The UI should not collapse those into `Delivered`.

## Validation still required on Cachy

The one required liveness check on 2026-07-22 reached `cachy` but SSH authentication failed:

```text
efox@cachy: Permission denied (publickey,password).
down
```

So no live Plasma behavior was validated. Primary documentation and current KDE source establish the API findings above, but these conclusions still need a real session check:

1. Confirm the installed Plasma, KWin, `xdg-desktop-portal`, KDE portal, KF6, Qt, AT-SPI, libei, and ydotool versions. Introspect portal interface versions at runtime.
2. Bind one Global Shortcuts action and record press, hold, release, auto-repeat, shortcut reconfiguration, daemon restart, portal restart, and lock-screen behavior.
3. Verify that the LayerShellQt popup never changes the focused AT-SPI object. Then deliberately change focus during transcription and confirm that synthetic delivery is withheld.
4. Build a matrix for Kate/KWrite, Firefox, Chromium, VS Code, a JetBrains IDE, LibreOffice, Konsole, a GTK editor, XWayland, and Qt password fields. Record app identity, focused control, caret, selections, bounded text, direct edit, readback, and text events separately.
5. Test keyboard-only Remote Desktop approval, persistence, libei key delivery, layouts, modifiers, session notification, and revocation without selecting a screen stream.
6. Test Paste Rules for `Ctrl+V`, `Ctrl+Shift+V`, direct AT-SPI edit, and Copy. Include a focus race and a destroyed Target.
7. Measure paste readback and clipboard restoration with slow native Wayland and XWayland Targets. A fixed delay is not enough evidence.
8. Verify that password roles are denied and document the unresolved terminal-password case. Confirm that no surrounding text or post-edit data reaches logs.
9. For each app that emits text events, distinguish user correction, application autoformat, undo, multi-cursor edits, and unrelated changes. Only the unambiguous cases qualify for a Learned Correction.

Until that matrix exists, label AT-SPI direct edit, verification, and brief edit observation as experimental per application. Global Shortcuts press/release, Copy, and focus-bound virtual input are supported mechanisms with known limits. They are not evidence that every Target supports the higher profiles.
