# Releasing the Android client

## Signing key

Release builds are signed with `~/.config/speecher-android/release.jks` (alias `speecher`). Its password is in `release.password` in the same directory. Both files stay outside the repo and are readable only by their owner.

Back up both files somewhere safe. Android only installs an update signed with the same key as the installed app, so losing the key means every existing install has to be removed and set up again.

Certificate SHA-256: `21:87:6E:E5:A4:53:02:D6:A2:68:E6:DA:C9:76:D1:50:16:98:D2:A0:43:43:97:E4:A5:9D:62:3A:91:CD:22:05`

## Building

```sh
cd android
./gradlew check :app:assembleRelease
```

The APK is written to `app/build/outputs/apk/release/app-release.apk`. If the key files are missing, the build still succeeds but the APK is unsigned.

## Publishing an update

Android releases are tagged `android-v<versionName>` and published with `--latest=false`. The desktop updater reads the repository's latest release, and the desktop release workflow builds on `v*` tags, so an Android release must not take either.

1. Bump `versionCode` and `versionName` in `app/build.gradle.kts`, write the release notes in `docs/android/releases/<versionName>.md`, and merge both to master.
2. Tag the merge and push the tag: `git tag android-v0.2.0 && git push origin android-v0.2.0`.

`.github/workflows/android-release.yml` checks that the tag matches `versionName`, builds and signs the APK, checks its certificate against the SHA-256 above, and creates the GitHub release with `Speecher-<version>.apk` attached. The notes file becomes the release body, which the app's What's New page shows after the update; start it at `##`, since the page supplies the title. Without the file, the body is a link to the install instructions.

## Nightly Builds

Every master push that touches `android/` also runs the workflow. It builds and signs the APK as above with the version `<last android-v tag, patch + 1>-nightly.<run number>+g<commit>`, so a nightly sorts above the release before it and below the next one, and publishes it as a prerelease under the moving `android-nightly` tag, replacing the previous nightly. The release body starts with a `Version:` line, which the app reads because the tag carries no version.

The workflow reads the key from two repository secrets. Set them once:

```sh
base64 -w0 ~/.config/speecher-android/release.jks | gh secret set ANDROID_RELEASE_KEYSTORE
gh secret set ANDROID_RELEASE_PASSWORD < ~/.config/speecher-android/release.password
```

While it is open, the app checks GitHub's release list as often as Settings > Updates says (once a day unless changed, and never with automatic checks off; Check now works either way). On the Stable channel it takes the highest non-prerelease `android-v*` version; on Nightly the `android-nightly` build counts too. If that release has an APK attached and is newer than the installed version, Home shows an update card. Tapping Update downloads the APK, showing its progress, and installs it in place, waiting for a dictation in progress to end first. The first time, Android asks the user to let Speecher install apps, and they tap Update again after allowing it. After that, Android installs Speecher's own updates without a confirmation prompt.

## Installing on a phone

No computer is needed after the app is on the phone; setup is entirely on-device.

1. Download and open the APK on the phone (allow "install unknown apps" for the browser once).
2. Open Speecher and work through the setup list: sign in, allow the microphone, turn on the Speecher keyboard, turn on the dictation button (its accessibility service).
3. Android may block turning on the dictation-button accessibility service for a sideloaded app. If it does, open App info for Speecher, tap the three-dot menu, choose **Allow restricted settings**, then turn the service on. This is the only friction, and it needs no computer.

The accessibility service is what lets the chip switch to the Speecher keyboard and back; there is no adb grant.
