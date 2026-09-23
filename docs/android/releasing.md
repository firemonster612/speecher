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

1. Bump `versionCode` and `versionName` in `app/build.gradle.kts`.
2. Build the release APK.
3. Create a GitHub release tagged `v<versionName>` and attach the APK.

The app checks the latest GitHub release when it opens, at most once a day. If the release's version is newer than the installed one, Home shows an update row.

## Installing on a phone

No computer is needed after the app is on the phone; setup is entirely on-device.

1. Download and open the APK on the phone (allow "install unknown apps" for the browser once).
2. Open Speecher and work through the setup list: sign in, allow the microphone, turn on the Speecher keyboard, turn on the dictation button (its accessibility service).
3. Android may block turning on the dictation-button accessibility service for a sideloaded app. If it does, open App info for Speecher, tap the three-dot menu, choose **Allow restricted settings**, then turn the service on. This is the only friction, and it needs no computer.

The accessibility service is what lets the chip switch to the Speecher keyboard and back; there is no adb grant.
