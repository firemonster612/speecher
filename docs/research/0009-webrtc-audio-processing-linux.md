# webrtc-audio-processing on Linux

Date: 2026-10-07. Spike for [issue #228](https://github.com/firemonster612/speecher/issues/228).
Package versions below are the amd64 or x86_64 versions checked on this date.

- Target the 1.x API, specifically 1.3, for the first Linux implementation.
  Install `libwebrtc-audio-processing-dev` in the existing Debian forky AppImage
  build image and link through `pkg-config webrtc-audio-processing-1`. Bundle
  the resulting system library with the AppImage's existing dependency closure.
  This avoids a new Meson/source build in the release pipeline. Version 1.3
  already has AEC3: upstream's `AudioProcessingImpl::InitializeEchoController`
  constructs `EchoCanceller3` when echo cancellation is enabled and
  `mobile_mode` is false. [Implementation at v1.3](https://gitlab.freedesktop.org/pulseaudio/webrtc-audio-processing/-/blob/v1.3/webrtc/modules/audio_processing/audio_processing_impl.cc).
- [Debian trixie](https://packages.debian.org/trixie/libwebrtc-audio-processing-dev)
  ships 1.3-3+b1, API 1.x. The development package is
  `libwebrtc-audio-processing-dev`, the runtime package is
  `libwebrtc-audio-processing-1-3`, and the pkg-config module is
  `webrtc-audio-processing-1`.
- [Debian forky](https://packages.debian.org/forky/libwebrtc-audio-processing-dev)
  ships 1.3-3+b3, API 1.x, with the same package names. A fresh apt update and
  install inside `debian:forky` confirmed this exact version. The package named
  `libwebrtc-audio-processing-1-dev` has no installation candidate.
- [Ubuntu LTS package indexes](https://packages.ubuntu.com/search?keywords=libwebrtc-audio-processing&searchon=names&suite=all&section=all)
  list 26.04 Resolute at 1.3-3build2, API 1.x, in
  `libwebrtc-audio-processing-dev` and `libwebrtc-audio-processing-1-3`.
  Its [development file list](https://packages.ubuntu.com/resolute/amd64/libwebrtc-audio-processing-dev/filelist)
  confirms `webrtc-audio-processing-1.pc`. Ubuntu 24.04 Noble ships
  0.3.1-0ubuntu6 and 22.04 Jammy ships 0.3.1-0ubuntu5. Those are the older
  0.3 API, neither 1.x nor 2.x, with runtime package
  `libwebrtc-audio-processing1`. They cannot supply this 1.3 build dependency.
- [Fedora 44](https://packages.fedoraproject.org/pkgs/webrtc-audio-processing/webrtc-audio-processing/),
  the current released Fedora, ships 2.1-5.fc44, API 2.x. Its
  [development package](https://packages.fedoraproject.org/pkgs/webrtc-audio-processing/webrtc-audio-processing-devel/fedora-44.html)
  is `webrtc-audio-processing-devel` and provides
  `pkgconfig(webrtc-audio-processing-2)`. Fedora 43 still lists 1.3-9.fc43.
- [Arch Extra](https://archlinux.org/packages/extra/x86_64/webrtc-audio-processing/)
  ships `webrtc-audio-processing` 2.1-9, API 2.x. Its
  [file list](https://archlinux.org/packages/extra/x86_64/webrtc-audio-processing/files/)
  includes `webrtc-audio-processing-2.pc` and
  `libwebrtc-audio-processing-2.so.1`.
- A 1.x target does not build against Fedora 44 or Arch's 2.x package merely
  by changing the pkg-config name. Native builds on those distributions need
  a separately supplied 1.3 dependency, or a later verified 2.x adaptation.
  The AppImage carries its own 1.x library. Upstream
  [2.1's Meson configuration](https://gitlab.freedesktop.org/pulseaudio/webrtc-audio-processing/-/blob/v2.1/meson.build)
  requires C++17, Meson >= 0.63 and Abseil >= 20240722. Forky's packaged
  development dependency supplied Abseil 20260526.0-2+b1 in this probe, so
  that minimum is available if a future change chooses a source build of 2.1.
- `packaging/appimage/Dockerfile` starts with `FROM debian:forky` and installs
  build dependencies as root before `USER builder`. Adding the actual
  `libwebrtc-audio-processing-dev` package to that apt list is sufficient to
  provide the tested headers, runtime library and pkg-config metadata.
  The spike used the same base image with only g++, pkg-config and this
  development package installed. It did not build the full Speecher AppImage.
- `packaging/build-appimage.sh` runs `ldd` on Speecher, copies each resolved
  library unless `skip_library` excludes it, and repeats this for the copied
  libraries. WebRTC is not excluded. Once Speecher calls and links the library,
  the script will copy `libwebrtc-audio-processing-1.so.3` to `AppDir/usr/lib`.
  It then gives libraries `$ORIGIN` RUNPATH and the executable
  `$ORIGIN/../lib`. A scratch test extracted the script's existing copy
  functions unchanged and ran them on the linked probe staged as
  `usr/bin/speecher`. The copied closure contained the WebRTC .so,
  `libstdc++.so.6` and `libgcc_s.so.1`, with no unresolved dependencies.
  Installing the package alone does not bundle it: it must be reachable in
  the linked binary's dependencies. A library loaded only through `dlopen`
  would require an explicit packaging step.
- Version 1.3's [upstream COPYING](https://gitlab.freedesktop.org/pulseaudio/webrtc-audio-processing/-/blob/v1.3/COPYING)
  is BSD-3-Clause, copyright Google. The installed Debian copyright file
  confirms this and records FFTPACK, BSD-2-Clause and other permissive
  third-party notices. Include that copyright file in distributed notices.
  Abseil is Apache-2.0. It is a development dependency here; this exact probe's
  `ldd` closure had no separate Abseil .so. Preserve any Abseil notices required
  by the final linked artifact, and check its actual dependency closure.
- The minimal program is `.scratch/webrtc-228/probe.cpp`, deliberately
  uncommitted. It creates `AudioProcessing` through `AudioProcessingBuilder`,
  enables echo cancellation with `mobile_mode=false`, initializes it, supplies
  one 10 ms mono float render frame at 48 kHz to `ProcessReverseStream`, sets
  stream delay to zero, and supplies a synthetic echo capture frame to
  `ProcessStream`. It checks return values and all 480 output samples for
  finiteness. Compile command inside the forky container:
  `g++ -std=c++17 -Wall -Wextra probe.cpp -o probe $(pkg-config --cflags --libs webrtc-audio-processing-1)`.
- The container build and execution passed. pkg-config reported `1.3` and the
  program printed
  `AEC enabled=1 mobile_mode=0 initialize=0 reverse=0 delay=0 capture=0 finite_samples=480`.
  `ldd` resolved `libwebrtc-audio-processing-1.so.3`. An initial build with
  `-Werror` failed on an unused parameter in the installed WebRTC header;
  the successful command above retained warnings without treating dependency
  header warnings as errors. Logs and the scratch Dockerfile are under
  `.scratch/webrtc-228/`. This verifies installation, linking and one frame
  pair. Echo suppression quality, delay alignment and sustained processing
  still need a real-call test with the captured system audio reference.
