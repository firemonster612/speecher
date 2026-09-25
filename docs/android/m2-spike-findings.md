# M2 platform spikes (#124): findings

Device: Android emulator, Pixel 7 profile, API 37 (Android 17), serial emulator-5554, Gboard as the stock keyboard.
Spike code: one throwaway app, `app.speecher.spike` (not kept in the repo). It contains the accessibility chip service, the stub IME, a trigger activity and a broadcast receiver. It targets SDK 37 with minSdk 31, the same as `android/app`.
Every timing below comes from `SystemClock.uptimeMillis()` in the spike process or from `logcat -v monotonic`. They share a clock.

All three spikes work. Each one needed at least one change the brief did not anticipate. Those changes are listed under "Gotchas" and affect the v1 design.

## Spike 1: chip overlay above the keyboard. Works.

Screenshots (in `spikes/`): `s1-chip-shown.png` (Gboard up, chip docked above its top-right), `s1-chip-resize.png` (the emoji panel made the keyboard taller and the chip followed), `s1-chip-hidden.png` (after BACK, keyboard and chip both gone).

APIs used:
- `AccessibilityService` configured with `typeWindowsChanged|typeViewFocused`, `flagRetrieveInteractiveWindows` and `canRetrieveWindowContent`.
- On every `TYPE_WINDOWS_CHANGED`: `getWindows()`, keep the one with `type == AccessibilityWindowInfo.TYPE_INPUT_METHOD`, then `getRegionInScreen(Region)` (API 33+).
- Chip: a `TextView` added with `WindowManager.addView` as `TYPE_ACCESSIBILITY_OVERLAY` with `FLAG_NOT_FOCUSABLE | FLAG_NOT_TOUCH_MODAL`, positioned at `x = ime.right - w - 16`, `y = ime.top - h - 16`, and moved with `updateViewLayout`.

What happened:
- For Gboard, `getRegionInScreen` and `getBoundsInScreen` returned the same rect: `(0,1517)-(1080,2400)`. Its window title is `English (US) (QWERTY)`, and `Emoji keyboard` on the emoji panel.
- Show. The chip was added 107 ms after `input tap` on the Settings search field (tap at 1612.33 s, added at 1612.437 s). At that moment the keyboard had only begun sliding in, with region top at 2176. Region updates then moved the chip twice more (1694 at +48 ms, 1412 at +216 ms) before it settled. It follows the slide in steps, not smoothly frame by frame.
- Hide. BACK went at 1590.45 s. The chip followed the keyboard down once (top 1663), got `ime gone` and was removed at 1590.701 s, about 250 ms after the keypress.
- Resize. Switching Gboard to its emoji panel reported region top 1192, and the chip moved to y=1087 (`s1-chip-resize.png`).
- Tapping the chip while Gboard was up left the text field focused. `dumpsys input_method` showed the same `mServedView` (`open_search_view_edit_text`) before and after the tap.
- One keyboard show produced 6 to 20 `TYPE_WINDOWS_CHANGED` events inside about 300 ms, with change flags 0x1, 0x2, 0x4, 0x8, 0x10 and 0x200.

Gotchas that change v1:
1. **Overlay coordinates are offset by default.** With plain `LayoutParams`, the chip landed 136 px lower than requested and covered Gboard's toolbar: requested y=1412, `dumpsys window` showed `frame=[781,1548]` with `parent=[0,136]`. Fix: set `fitInsetsTypes = 0` and `layoutInDisplayCutoutMode = LAYOUT_IN_DISPLAY_CUTOUT_MODE_ALWAYS`. Setting only `fitInsetsTypes = 0` was not enough, since the parent frame still started at 136. With both, the parent became `[0,0]` and the frame matched the requested position.
2. **agent-device's UiAutomation client unbinds the service.** While that client was connected, the system unbound every accessibility service: `dumpsys accessibility` showed `Ui Automation[...]` and `Bound services:{}`, and the chip got no events. The client here was agent-device's snapshot instrumentation. The service rebound as soon as that instrumentation ended. Clients that connect without `UiAutomation.FLAG_DONT_SUPPRESS_ACCESSIBILITY_SERVICES` suppress accessibility services, so automation tools on a user's device can silently disable the chip. Whether a test driver can use that flag to test the chip is untested.
3. **The chip also appears above our own IME.** After the swap, the service saw a `TYPE_INPUT_METHOD` window with `title=null` and re-added the chip above the stub panel (`s2-swapped.png`). v1 must suppress the chip while our IME is the active one. I did not test which signal is most reliable for that. Candidates are `Settings.Secure.DEFAULT_INPUT_METHOD`, state the IME publishes itself, or the window root's package.
4. Debounce or coalesce the event burst. Also expect the chip to be added before the keyboard reaches its final position. A short fade-in, or waiting until the region stops changing, would hide the stepping.
5. The chip service and the IME run in one process in this spike. When the IME crashed, the chip service went down too and reconnected (`chip: service connected` right after the crash). If they share a process in v1, an IME crash takes the chip with it.

## Spike 2: silent IME swap. Works, after three fixes.

Screenshots (in `spikes/`): `s2-before.png` (Gboard, text in field, chip up), `s2-swapped.png` (stub panel in place of Gboard, same field still focused with the cursor and existing text), `s2-switched-back.png` (Gboard back with the text intact, plus "hello" that the stub committed), and `s2-flicker-sequence.png` (frame-by-frame swap recording).

APIs used:
- `adb shell pm grant app.speecher.spike android.permission.WRITE_SECURE_SETTINGS`, which succeeded (exit 0).
- Swap in. Read `Settings.Secure.DEFAULT_INPUT_METHOD` and `"selected_input_method_subtype"`, and store both. If our IME is missing from `InputMethodManager.getEnabledInputMethodList()`, write `ENABLED_INPUT_METHODS` with it appended. Then `Settings.Secure.putString(DEFAULT_INPUT_METHOD, "app.speecher.spike/spike.StubIme")`.
- Swap back from inside the IME: `InputMethodService.switchInputMethod(prevId, subtype)`. The subtype is found by matching the saved `selected_input_method_subtype` against `InputMethodSubtype.hashCode()` in `getEnabledInputMethodSubtypeList(info, true)`.

Timing, measured from the `putString` call and taken from two clean runs:

| Event | Run A | Run B |
| --- | --- | --- |
| `putString(DEFAULT_INPUT_METHOD)` returns true | 0 ms | 0 ms |
| stub `onCreate` (cold) | +19 ms | +26 ms |
| `onStartInput` on the same field id (`2131428057`) | +29 ms | +36 ms |
| `onStartInputView` and `onWindowShown` | +34 ms | +40 ms |

Swap back: `switchInputMethod` was called at 1899.117 s, it returned at +8 ms, and the stub got `onFinishInputView` at +98 ms. Afterwards, `default_input_method` was Gboard again and `selected_input_method_subtype` was back at `1594443099`. While the stub was active that value had been `-1`.

Focus: through swap in, commit and swap back, `mServedView` stayed the same `open_search_view_edit_text`. The typed text survived both swaps. `commitText("hello", 1)` from the stub landed at the cursor. `s2-switched-back.png` shows `abcabcabchelloabc`.

Flicker: measured frame by frame in the "Frame-level flicker" section below.

Gotchas that change v1:
1. **`ENABLED_INPUT_METHODS` cannot be read at targetSdk 37.** `Settings.Secure.getString(ENABLED_INPUT_METHODS)` threw `SecurityException: Settings key: <enabled_input_methods> is only readable to apps with targetSdkVersion lower than or equal to: 33`, which killed the process. Writing it with WRITE_SECURE_SETTINGS worked (`putString` returned true). Read the enabled list from `InputMethodManager.getEnabledInputMethodList()` instead. Reading `DEFAULT_INPUT_METHOD` and `selected_input_method_subtype` worked.
2. **The panel stays hidden unless `onEvaluateInputViewShown()` returns true.** After the first swap the stub bound to the field (`onStartInput`) but never got `onStartInputView`. `dumpsys input_method` showed `mIsInputViewShown=false`, and ImeTracker logged `TYPE_SHOW - STATUS_FAIL ... PHASE_IME_ON_SHOW_SOFT_INPUT_TRUE`. `super.onEvaluateInputViewShown()` returned false because the emulator's configuration reports a hardware keyboard (`keysexposed-qwerty` in `am get-config`). Gboard still showed, so Gboard evidently overrides this too. Setting `show_ime_with_hard_keyboard=1` did not help. Overriding the method to return true fixed it immediately. This is the tested remedy on the emulator. A phone with a Bluetooth keyboard connected probably takes the same path, but that is untested, so v1 should override it and verify on the phone.
3. **`switchInputMethod` needs package visibility.** Without a `<queries>` entry, `getEnabledInputMethodList()` from our package returned only the Google TTS voice IME and our own. Gboard was missing, and `switchInputMethod("…LatinIME", …)` threw `RuntimeException: Unknown id`. That crashed the IME in the first run. Adding `<queries><intent><action android:name="android.view.InputMethod"/></intent></queries>` made Gboard visible, and the switch then succeeded with its subtype. The run changed only that, so package visibility is the likely cause, not an isolated one. `switchToPreviousInputMethod()` worked even without `<queries>`, so it is a usable fallback.
4. **Rewriting `ENABLED_INPUT_METHODS` drops subtypes.** The stored value carries per-IME subtypes (`…LatinIME;1594443099:…`), but the spike rebuilt it from `InputMethodInfo.getId()` alone, which loses the `;subtype` suffixes. v1 must not rewrite this setting from the IMM list. Enable our IME once during onboarding through the system settings screen (or `adb shell ime enable`), and never write the setting at runtime.
5. **A crash can strand the user in our IME.** If our process dies while it is the default IME, the in-panel switch-back is gone and nothing restores Gboard. v1 must restore the saved IME when the process restarts (`onCreate` of the IME and of the chip service) and keep the system keyboard-switch key working. Process death is untested.
6. Swapping unconditionally leaves our IME enabled in the system list. The spike appended itself to `ENABLED_INPUT_METHODS`, and it then shows up in the system IME picker. v1 needs a policy for this: leave it enabled, or remove it after switching back.

## Spike 3: mic capture inside the IME. Works.

Screenshots (in `spikes/`): `s3-permission-dialog.png` (the runtime prompt, raised from the app's activity), `s3-recorded.png` (stub panel over Settings showing the capture result). The raw capture (96,000 bytes, s16le, 16 kHz, mono) was not kept.

APIs used: `AudioRecord(MediaRecorder.AudioSource.VOICE_RECOGNITION, 16000, CHANNEL_IN_MONO, ENCODING_PCM_16BIT, max(minBuffer, 16000))` on a background thread, then `startRecording()` and blocking `read(short[1600])` until 48,000 samples, written to `filesDir/capture.pcm`. `getMinBufferSize` returned 1024 bytes.

Permission flow, as observed:
1. With RECORD_AUDIO not granted, recording from the IME (with the panel shown over Settings) failed. `AudioRecord.state` was 0 and `startRecording()` threw `IllegalStateException`. audioserver logged `getInputForAttr ... missing perms for source 6`. No system prompt appeared.
2. The IME is a service and cannot show the runtime prompt. I raised it from `MainActivity` with `requestPermissions`. The dialog offered "While using the app", "Only this time" and "Don't allow". I chose "While using the app". `dumpsys package` then showed `granted=true`, and `appops get` showed `RECORD_AUDIO: foreground`.
3. Back in Settings with the stub panel shown and our activity no longer on screen, the same recording succeeded. `state=1`, `recordingState=3` (RECORDING), `startRecording` took 2 ms, the read loop returned 48,000 samples in 3,034 ms wall time, and the file is 96,000 bytes. `dumpsys audio` logged `src:VOICE_RECOGNITION not silenced pack:app.speecher.spike` for both the update and the stop.

The emulator's virtual mic delivered near-silence (peak |sample| = 8, values between -1 and 5). The emulator audio HAL logged once that it was `inserting 8000 us of silence` because its producer thread was late.

Android 17 wrinkles: none seen on this path. A while-in-use grant was honoured for a service whose IME window was visible over another app, and no foreground service was declared or needed. Not tested: recording after the IME window hides, the "Only this time" grant after the app leaves the foreground, and a real microphone.

Gotcha that changes v1: onboarding must request RECORD_AUDIO from an activity before the first dictation, because the IME cannot prompt. If permission is missing, the panel needs an in-panel error with an action that opens the app. `AudioRecord` fails fast (state 0), so detecting the failure is simple.

## Frame-level flicker

Measured with `adb shell screenrecord` (20 Mbit/s) while triggering the swap by broadcast, then decoded frame by frame. Contact sheet: `spikes/s2-flicker-sequence.png`. Frame times are video-relative.

| Video time | What is on screen |
| --- | --- |
| 1.120 s | Gboard, fully drawn |
| 1.139 s | No keyboard. Gboard is gone in a single frame, with no exit animation, and the app content has not moved yet. |
| 1.139 to 1.523 s | No keyboard at all, about 385 ms |
| 1.523 s | The stub panel appears at the bottom edge, faded |
| 1.523 to 1.657 s | Slide-in and fade-in, about 135 ms |
| 1.657 s | Stub fully shown |

**Result: the swap is not seamless.** For about 0.4 s the user sees no keyboard, then our panel slides in over about 0.13 s, roughly 0.5 s in total. The field keeps focus throughout, and the cursor was visible at 1.120 s and again at 1.657 s. This emulator has no GPU, uses software rendering, and starts the IME cold, so a real phone is probably faster. The gap is still long enough to read as a flash.

This changes the v1 design. The "keyboard morphs into our panel" illusion needs one of these:
- the chip gives feedback the moment it is tapped (shows the listening state and starts the mic), so the gap reads as "opening" rather than a glitch;
- the IME process is kept warm, so its window is not created cold;
- or both. Measure again on the real phone before choosing.

## Not tested, needed before v1

- Floating, split and one-handed Gboard, rotation, and whatever keyboard the phone's OEM ships. The chip anchors to the top-right of the region's bounding box, which can include empty space for non-rectangular keyboards.
- Password and other sensitive fields. v1 needs a rule for when the chip shows, based on the focused editor's input type.
- The restricted-settings step Android applies to accessibility services in sideloaded apps, on the real phone.
- Recording beyond a visible 3 s session: focus loss, screen lock, and dictation that outlives the IME window. That decides whether a microphone foreground service is needed.
- Process death during a swap.

## Claims from #124

| Claim | Result |
| --- | --- |
| An accessibility service detects keyboard visibility and bounds and docks a chip above it | Confirmed. Needs the `fitInsetsTypes`/cutout fix, and is disabled whenever UiAutomation is connected. |
| The chip tracks keyboard show, hide and resize | Confirmed. Show took about 107 ms to the first placement plus about 216 ms to settle, in steps. Hide took about 250 ms. The emoji-panel resize was tracked. |
| `getRegionInScreen()` gives the keyboard's bounds | Confirmed for Gboard. It matched `getBoundsInScreen`. |
| WRITE_SECURE_SETTINGS silent swap Gboard → stub mid-focus without losing focus | Confirmed. Our input view was up 34 to 40 ms after the write, on the same field, with text preserved. Requires the `onEvaluateInputViewShown` override (at least when a hardware keyboard is reported). |
| Swap back via `InputMethodService.switchInputMethod` | Confirmed, only with `<queries>` for `android.view.InputMethod`. Subtype restored. |
| Measure flicker | Measured. There is a visible gap of about 400 ms with no keyboard on screen, followed by the stub's slide-in animation (see "Frame-level flicker" above). |
| AudioRecord 16 kHz mono PCM16 capture inside an IME service | Confirmed. 3 s captured, not silenced, once RECORD_AUDIO was granted from an activity. |

## Test-rig notes (not product findings)

- agent-device replaces the default IME with its own helper (`com.callstack.agentdevice.imehelper/.TestInputMethodService`) whenever it drives the device, and its snapshot instrumentation unbinds accessibility services. The keyboard checks above used `adb shell ime set`, `input tap` and `screencap` instead.
- The emulator was left with Gboard as the default IME (subtype `1594443099`), the spike IME disabled, and accessibility services off. The spike app is still installed, and WRITE_SECURE_SETTINGS and RECORD_AUDIO are still granted to it.
