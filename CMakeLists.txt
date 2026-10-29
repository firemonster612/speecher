cmake_minimum_required(VERSION 3.24)

# The release job sets its own, MACOS_DEPLOYMENT_TARGET in
# .github/workflows/release.yml, for both Speecher and webrtc-audio-processing;
# keep the two in step.
if(CMAKE_HOST_APPLE AND NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET)
  set(CMAKE_OSX_DEPLOYMENT_TARGET "15.0" CACHE STRING
    "Minimum macOS version supported by Speecher")
endif()

project(speecher VERSION 0.1.1 LANGUAGES C CXX)

include(cmake/GitVersion.cmake)

if(APPLE)
  # The macOS platform backend is Objective-C++. Objective-C too, for ggml's
  # Metal backend: with only OBJCXX enabled CMake compiles its .m files as
  # Objective-C++, where they do not build.
  enable_language(OBJC OBJCXX)

  include(FetchContent)
  FetchContent_Declare(sparkle
    URL https://github.com/sparkle-project/Sparkle/releases/download/2.9.6/Sparkle-2.9.6.tar.xz
    URL_HASH SHA256=52bf9e88cdd972fc0c81501377a880e90d47031bd8ca5462488f843e2609e192
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  )
  FetchContent_MakeAvailable(sparkle)

  set(SPEECHER_SPARKLE_ED_PUBLIC_KEY "" CACHE STRING
    "Sparkle EdDSA public key embedded in the macOS app")
  set(SPEECHER_SPARKLE_PUBLIC_KEY_PLIST "")
  if(SPEECHER_SPARKLE_ED_PUBLIC_KEY)
    set(SPEECHER_SPARKLE_PUBLIC_KEY_PLIST
      "<key>SUPublicEDKey</key>\n\t<string>${SPEECHER_SPARKLE_ED_PUBLIC_KEY}</string>")
  endif()
  set(SPEECHER_SPARKLE_FRAMEWORK "${sparkle_SOURCE_DIR}/Sparkle.framework")
  add_library(Sparkle::Sparkle SHARED IMPORTED GLOBAL)
  # INTERFACE_INCLUDE_DIRECTORIES names the .framework directory itself: CMake
  # recognizes framework paths and emits -F/-iframework so that
  # #import <Sparkle/Sparkle.h> resolves when compiling consumers.
  set_target_properties(Sparkle::Sparkle PROPERTIES
    FRAMEWORK TRUE
    IMPORTED_LOCATION "${SPEECHER_SPARKLE_FRAMEWORK}/Sparkle"
    IMPORTED_SONAME "@rpath/Sparkle.framework/Versions/B/Sparkle"
    INTERFACE_INCLUDE_DIRECTORIES "${SPEECHER_SPARKLE_FRAMEWORK}"
  )
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)
if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

option(SPEECHER_WITH_LAYER_SHELL "Use layer-shell-qt when available" ON)
option(SPEECHER_WITH_QKEYCHAIN "Use QKeychain when available" ON)
option(SPEECHER_BUILD_TESTS "Build tests" ON)
# End-to-end hooks (stub providers, mic-gate skip) exist only for CI/E2E
# builds; distributed builds must not carry env-activated fake providers.
option(SPEECHER_E2E_HOOKS "Compile end-to-end test hooks" OFF)
option(SPEECHER_RELEASE_BUILD "Require every Linux release integration" OFF)
set(SPEECHER_WITH_KDE_DEFAULT OFF)
if(UNIX AND NOT APPLE)
  set(SPEECHER_WITH_KDE_DEFAULT ON)
endif()
option(SPEECHER_WITH_KDE "Require KDE integration on Linux"
  ${SPEECHER_WITH_KDE_DEFAULT})
if(SPEECHER_RELEASE_BUILD AND (NOT UNIX OR APPLE))
  message(FATAL_ERROR "SPEECHER_RELEASE_BUILD is the Linux AppImage release profile.")
endif()
if(SPEECHER_RELEASE_BUILD AND (NOT SPEECHER_WITH_KDE OR NOT SPEECHER_WITH_QKEYCHAIN))
  message(FATAL_ERROR
    "SPEECHER_RELEASE_BUILD requires SPEECHER_WITH_KDE=ON and SPEECHER_WITH_QKEYCHAIN=ON.")
endif()

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Widgets Network Multimedia)

function(speecher_assert_single_qt_prefix context)
  get_filename_component(qt6_dir_real "${Qt6_DIR}" REALPATH)
  get_filename_component(qt6_core_dir_real "${Qt6Core_DIR}" REALPATH)
  get_filename_component(qt6_cmake_prefix "${qt6_dir_real}" DIRECTORY)
  get_filename_component(qt6_core_cmake_prefix "${qt6_core_dir_real}" DIRECTORY)
  if(NOT qt6_cmake_prefix STREQUAL qt6_core_cmake_prefix)
    message(FATAL_ERROR
      "${context} mixed Qt installations: Qt6_DIR is '${Qt6_DIR}', but "
      "Qt6Core_DIR is '${Qt6Core_DIR}'. Pass -DCMAKE_PREFIX_PATH to a single "
      "Qt installation, or install KF6 and LayerShellQt packages built for "
      "Qt ${Qt6_VERSION_MAJOR}.${Qt6_VERSION_MINOR}.")
  endif()
  if(NOT Qt6Core_VERSION_MAJOR EQUAL Qt6_VERSION_MAJOR OR
     NOT Qt6Core_VERSION_MINOR EQUAL Qt6_VERSION_MINOR)
    message(FATAL_ERROR
      "${context} mixed Qt ${Qt6_VERSION_MAJOR}.${Qt6_VERSION_MINOR} with "
      "QtCore ${Qt6Core_VERSION_MAJOR}.${Qt6Core_VERSION_MINOR}. Pass "
      "-DCMAKE_PREFIX_PATH to a single Qt installation, or install matching "
      "KF6 and LayerShellQt packages.")
  endif()
endfunction()

# Qt 6 runs libraries built for its own or an older minor version, never a
# newer one. A package that imports Qt's private API (EXACT) needs its own
# minor; Debian ties those packages to one qt6-base-private-abi.
function(speecher_assert_package_qt_version package_name target_name)
  cmake_parse_arguments(PARSE_ARGV 2 arg "EXACT" "" "")
  if(arg_UNPARSED_ARGUMENTS)
    message(FATAL_ERROR "Unknown arguments: ${arg_UNPARSED_ARGUMENTS}")
  endif()
  get_target_property(imported_configs "${target_name}" IMPORTED_CONFIGURATIONS)
  foreach(config IN LISTS imported_configs)
    get_target_property(package_library "${target_name}" "IMPORTED_LOCATION_${config}")
    if(EXISTS "${package_library}")
      break()
    endif()
  endforeach()
  if(NOT EXISTS "${package_library}" OR NOT CMAKE_READELF)
    message(FATAL_ERROR
      "Could not inspect ${package_name}'s Qt build version. Pass "
      "-DCMAKE_PREFIX_PATH to a single Qt installation, or install matching "
      "KF6 and LayerShellQt packages.")
  endif()

  execute_process(
    COMMAND "${CMAKE_READELF}" --hex-dump=.qtversion "${package_library}"
    RESULT_VARIABLE readelf_result
    OUTPUT_VARIABLE qt_version_section
    ERROR_QUIET
  )
  # Qt stores QT_VERSION as patch, minor, major bytes in each Qt-linked ELF.
  string(REGEX MATCH
    "00000000[ \t]+00000000[ \t]+([0-9a-fA-F][0-9a-fA-F])([0-9a-fA-F][0-9a-fA-F])([0-9a-fA-F][0-9a-fA-F])[0-9a-fA-F][0-9a-fA-F]"
    qt_version_match "${qt_version_section}")
  if(NOT readelf_result EQUAL 0 OR NOT qt_version_match)
    message(FATAL_ERROR
      "Could not read ${package_name}'s Qt build version from "
      "'${package_library}'. Install a ${package_name} package built for Qt "
      "${Qt6_VERSION_MAJOR}.${Qt6_VERSION_MINOR}.")
  endif()

  set(package_qt_minor_hex "${CMAKE_MATCH_2}")
  set(package_qt_major_hex "${CMAKE_MATCH_3}")
  math(EXPR package_qt_minor "0x${package_qt_minor_hex}")
  math(EXPR package_qt_major "0x${package_qt_major_hex}")
  if(NOT package_qt_major EQUAL Qt6_VERSION_MAJOR
     OR package_qt_minor GREATER Qt6_VERSION_MINOR
     OR (arg_EXACT AND NOT package_qt_minor EQUAL Qt6_VERSION_MINOR))
    message(FATAL_ERROR
      "${package_name} was built for Qt ${package_qt_major}.${package_qt_minor}, "
      "but CMake found Qt ${Qt6_VERSION_MAJOR}.${Qt6_VERSION_MINOR}. Pass "
      "-DCMAKE_PREFIX_PATH to a single Qt installation, or install matching "
      "KF6 and LayerShellQt packages.")
  endif()
endfunction()

speecher_assert_single_qt_prefix("Qt discovery")
if(SPEECHER_RELEASE_BUILD)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(ATSPI REQUIRED IMPORTED_TARGET atspi-2 gobject-2.0)
  pkg_check_modules(WAYLAND_CLIENT REQUIRED IMPORTED_TARGET wayland-client)
  # The X11 single-key watcher reads raw key events through XInput2.
  pkg_check_modules(X11 REQUIRED IMPORTED_TARGET x11)
  pkg_check_modules(XI REQUIRED IMPORTED_TARGET xi)
  # The popup's input shape on X11.
  pkg_check_modules(XEXT REQUIRED IMPORTED_TARGET xext)
  # System audio capture reads the default output's monitor source.
  pkg_check_modules(PULSE REQUIRED IMPORTED_TARGET libpulse)
  # Echo cancellation takes system audio out of a Recording's microphone.
  pkg_check_modules(WEBRTC_APM REQUIRED IMPORTED_TARGET webrtc-audio-processing-1)
  pkg_get_variable(WAYLAND_PROTOCOLS_DATA_DIR wayland-protocols pkgdatadir)
else()
  find_package(PkgConfig QUIET)
endif()
if(PkgConfig_FOUND AND NOT SPEECHER_RELEASE_BUILD)
  pkg_check_modules(ATSPI QUIET IMPORTED_TARGET atspi-2 gobject-2.0)
  pkg_check_modules(WAYLAND_CLIENT QUIET IMPORTED_TARGET wayland-client)
  # QUIET like the neighbours: a Wayland-only host without libxi dev files
  # still builds, minus the XInput2 single-key backend.
  pkg_check_modules(X11 QUIET IMPORTED_TARGET x11)
  pkg_check_modules(XI QUIET IMPORTED_TARGET xi)
  pkg_check_modules(XEXT QUIET IMPORTED_TARGET xext)
  pkg_check_modules(PULSE QUIET IMPORTED_TARGET libpulse)
  pkg_check_modules(WEBRTC_APM QUIET IMPORTED_TARGET webrtc-audio-processing-1)
  pkg_get_variable(WAYLAND_PROTOCOLS_DATA_DIR wayland-protocols pkgdatadir)
endif()
# The Linux release profile requires echo cancellation above; the macOS
# release job, which has no profile, asks for it with this.
option(SPEECHER_REQUIRE_WEBRTC_AEC "Fail without webrtc-audio-processing" OFF)
if(SPEECHER_REQUIRE_WEBRTC_AEC AND NOT WEBRTC_APM_FOUND)
  message(FATAL_ERROR
    "SPEECHER_REQUIRE_WEBRTC_AEC needs webrtc-audio-processing-1 through pkg-config. "
    "On macOS, build it with packaging/macos/build-webrtc-audio-processing.sh.")
endif()
if(SPEECHER_RELEASE_BUILD)
  find_package(Qt6 6.5 REQUIRED COMPONENTS WebSockets)
else()
  find_package(Qt6 6.5 QUIET COMPONENTS WebSockets)
endif()
# The Qt Quick UI (cmake/QuickUi.cmake). Its QML needs 6.9, for
# QtQuick.Effects' RectangularShadow.
find_package(Qt6 6.9 REQUIRED COMPONENTS Qml Quick QuickTemplates2)
if(UNIX AND NOT APPLE)
  find_package(Qt6 6.5 REQUIRED COMPONENTS DBus)
  if(SPEECHER_WITH_KDE)
    find_package(KF6GlobalAccel REQUIRED)
    find_package(LayerShellQt REQUIRED)
  else()
    find_package(KF6GlobalAccel QUIET)
    if(SPEECHER_WITH_LAYER_SHELL)
      find_package(LayerShellQt QUIET)
    endif()
  endif()

  speecher_assert_single_qt_prefix("KDE package discovery")
  if(KF6GlobalAccel_FOUND)
    speecher_assert_package_qt_version("KF6GlobalAccel" "KF6::GlobalAccel" EXACT)
  endif()
  if(LayerShellQt_FOUND)
    speecher_assert_package_qt_version("LayerShellQt" "LayerShellQt::Interface" EXACT)
  endif()
elseif(SPEECHER_WITH_LAYER_SHELL)
  find_package(LayerShellQt QUIET)
endif()
if(SPEECHER_BUILD_TESTS)
  find_package(Qt6 6.5 REQUIRED COMPONENTS Test)
endif()

set(SPEECHER_DOMAIN_SOURCES
  src/core/BindingProcessor.cpp
  src/core/bindings/BindingMatcher.cpp
  src/core/bindings/NoBindDirectiveParser.cpp
  src/core/CleanupModelCatalog.cpp
  src/core/CliToolDiscovery.cpp
  src/core/ColorSchemes.cpp
  src/core/DictationRecord.cpp
  src/core/InsightsExport.cpp
  src/core/InsightsLog.cpp
  src/core/InsightsSummary.cpp
  src/core/LearnedCorrection.cpp
  src/core/EndpointSettings.cpp
  src/core/LocalModelCatalog.cpp
  src/core/OutputMethod.cpp
  src/core/ProviderChain.cpp
  src/core/ProviderTable.cpp
  src/core/OutputFormat.cpp
  src/core/PasteRules.cpp
  src/core/ProviderReadiness.cpp
  src/core/ProviderRatings.cpp
  src/core/ReleaseNotes.cpp
  src/core/SettingsStore.cpp
  src/core/ShortcutBinding.cpp
  src/core/settings/CorrectionSettingsCodec.cpp
  src/core/settings/SettingsCodecs.cpp
  src/core/settings/SettingsFile.cpp
  src/core/settings/SpokenLanguages.cpp
  src/core/settings/VocabularySettingsCodec.cpp
  src/core/SecretStore.cpp
  src/core/TranscriptState.cpp
  src/core/Target.cpp
  src/core/VocabularyLimit.cpp
  src/core/Vocabulary.cpp
  src/core/VocabularyEdits.cpp
  src/core/WordPreview.cpp
  src/core/WritingProfileEdits.cpp
  src/core/WritingProfileProviders.cpp
  src/dictation/DictationTypes.cpp
  src/dictation/PopupPresentation.cpp
  src/dictation/SelectionEditPresentation.cpp
  src/recording/SystemAudioPresentation.cpp
)

set(SPEECHER_DOMAIN_HEADERS
  src/core/AppSettings.h
  src/core/BindingProcessor.h
  src/core/bindings/BindingMatcher.h
  src/core/bindings/NoBindDirectiveParser.h
  src/core/CleanupModelCatalog.h
  src/core/CliToolDiscovery.h
  src/core/ColorSchemes.h
  src/core/DictationRecord.h
  src/core/EchoCanceller.h
  src/core/InsightsExport.h
  src/core/InsightsLog.h
  src/core/InsightsSummary.h
  src/core/LearnedCorrection.h
  src/core/LocalModelCatalog.h
  src/core/OutputMethod.h
  src/core/OutputFormat.h
  src/core/PasteRules.h
  src/core/ProviderReadiness.h
  src/core/ProviderRatings.h
  src/core/ReleaseNotes.h
  src/core/SettingsStore.h
  src/core/ShortcutBinding.h
  src/core/settings/CorrectionSettingsCodec.h
  src/core/settings/SettingsCodecs.h
  src/core/settings/SettingsFile.h
  src/core/settings/SettingsKeys.h
  src/core/settings/SpokenLanguages.h
  src/core/settings/VocabularySettingsCodec.h
  src/core/SecretStore.h
  src/core/SettingsEdit.h
  src/core/TranscriptState.h
  src/core/Target.h
  src/core/VocabularyLimit.h
  src/core/Vocabulary.h
  src/core/VocabularyEdits.h
  src/core/WordPreview.h
  src/core/WritingProfileEdits.h
  src/core/WritingProfileProviders.h
  src/dictation/DictationTypes.h
  src/dictation/PopupPresentation.h
  src/dictation/SelectionEditPresentation.h
  src/recording/SystemAudioPresentation.h
)

set(SPEECHER_WORKFLOW_SOURCES
  src/dictation/DictationSession.cpp
  src/dictation/DictationPorts.cpp
  src/dictation/StartupPreparationRunner.cpp
  src/dictation/TranscriptPipeline.cpp
  src/providers/ProviderRegistry.cpp
  src/providers/SpeechChain.cpp
)

set(SPEECHER_WORKFLOW_HEADERS
  src/dictation/DictationPorts.h
  src/dictation/DictationSession.h
  src/dictation/StartupPreparationRunner.h
  src/dictation/TranscriptPipeline.h
  src/providers/ProviderRegistry.h
  src/providers/SpeechChain.h
)

set(SPEECHER_PROVIDER_SOURCES
  src/providers/StreamingRefinement.cpp
  src/providers/ProviderFailureClassification.cpp
  src/providers/ChatCompletionsRefiner.cpp
  src/providers/CustomEndpoints.cpp
  src/providers/EndpointSpeechTranscriber.cpp
  src/providers/EndpointTranscriptRefiner.cpp
  src/providers/LocalRunner.cpp
  src/providers/AnthropicApiRefiner.cpp
  src/providers/AnthropicAuthProvider.cpp
  src/providers/AnthropicTranscriptRefiner.cpp
  src/providers/CodexCredentialStorage.cpp
  src/providers/NativeCredentialStorage.cpp
  src/providers/ClaudeCredentialStorage.cpp
  src/providers/ClaudeCredentials.cpp
  src/providers/ClaudeSpeechTranscriber.cpp
  src/providers/ClaudeVoiceClient.cpp
  src/providers/ClaudeVoiceProtocol.cpp
  src/providers/CliProxyCredentials.cpp
  src/providers/ProviderSignIn.cpp
  src/providers/SignInCheck.cpp
  src/providers/CodexDictationClient.cpp
  src/providers/CodexSpeechTranscriber.cpp
  src/providers/LocalModelStore.cpp
  src/providers/OpenAiAuthProvider.cpp
  src/providers/OpenAiRefiner.cpp
  src/providers/OpenAiTranscriptRefiner.cpp
  src/providers/TranscriptRefinementPrompt.cpp
)
if(SPEECHER_E2E_HOOKS)
  list(APPEND SPEECHER_PROVIDER_SOURCES src/providers/E2EProviders.cpp)
  add_compile_definitions(SPEECHER_E2E_HOOKS=1)
endif()

set(SPEECHER_PROVIDER_HEADERS
  src/providers/StreamingRefinement.h
  src/providers/ProviderFailureClassification.h
  src/providers/BlockingHttp.h
  src/providers/ChatCompletionsRefiner.h
  src/providers/CustomEndpoints.h
  src/providers/EndpointSpeechTranscriber.h
  src/providers/EndpointTranscriptRefiner.h
  src/providers/LocalRunner.h
  src/providers/PcmWav.h
  src/providers/ServerSentEvents.h
  src/providers/AnthropicApiRefiner.h
  src/providers/AnthropicAuthProvider.h
  src/providers/AnthropicTranscriptRefiner.h
  src/providers/CodexCredentialStorage.h
  src/providers/NativeCredentialStorage.h
  src/providers/ClaudeCredentialStorage.h
  src/providers/ClaudeCredentials.h
  src/providers/ClaudeSpeechTranscriber.h
  src/providers/ClaudeVoiceClient.h
  src/providers/ClaudeVoiceProtocol.h
  src/providers/CliProxyCredentials.h
  src/providers/ProviderProbe.h
  src/providers/ProviderSignIn.h
  src/providers/SignInCheck.h
  src/providers/CodexDictationClient.h
  src/providers/CodexSpeechTranscriber.h
  src/providers/LocalModelStore.h
  src/providers/OpenAiAuthProvider.h
  src/providers/OpenAiRefiner.h
  src/providers/OpenAiTranscriptRefiner.h
  src/providers/TranscriptRefinementPrompt.h
  src/providers/WebSocketClose.h
)

set(SPEECHER_PLATFORM_SOURCES
  src/output/ClipboardDelivery.cpp
  src/output/QtClipboardDelivery.cpp
  src/output/TextDelivery.cpp
  src/platform/CorrectionDiff.cpp
  src/platform/HotKeyShortcutBinder.cpp
  src/platform/RoutingShortcutBinder.cpp
  src/platform/SessionShortcutBinder.cpp
  src/platform/ScreenshotImage.cpp
  src/platform/SingleKeyShortcutBinder.cpp
  src/platform/audio/AudioPcmConverter.cpp
  src/platform/audio/LoopbackPcm.cpp
  src/platform/audio/LoopbackReopen.cpp
  src/platform/audio/QtAudioInput.cpp
  src/platform/audio/WavFileAudioInput.cpp
  src/platform/FallbackPopupPositioner.cpp
)

set(SPEECHER_PLATFORM_HEADERS
  src/output/ClipboardDelivery.h
  src/output/ClipboardSnapshot.h
  src/output/QtClipboardDelivery.h
  src/output/TextDelivery.h
  src/platform/AccessibilityState.h
  src/platform/MicrophoneAccess.h
  src/platform/SessionKeyGrab.h
  src/platform/CorrectionDiff.h
  src/platform/FallbackPopupPositioner.h
  src/platform/GlobalShortcutBinder.h
  src/platform/HotKeyShortcutBinder.h
  src/platform/PopupPositioner.h
  src/platform/WindowBackdrop.h
  src/platform/PopupSurface.h
  src/platform/RoutingShortcutBinder.h
  src/platform/SessionShortcutBinder.h
  src/platform/ScreenshotImage.h
  src/platform/SingleKeyShortcutBinder.h
  src/platform/audio/AudioPcmConverter.h
  src/platform/audio/E2EAudioInput.h
  src/platform/audio/LoopbackPcm.h
  src/platform/audio/LoopbackReopen.h
  src/platform/audio/QtAudioInput.h
  src/platform/audio/WavFileAudioInput.h
)

set(SPEECHER_PLATFORM_LINUX_SOURCES
  src/output/HelperInstall.cpp
  src/output/WaylandClipboardOwner.cpp
  src/output/WaylandClipboardProcess.cpp
  src/output/WlClipboardDelivery.cpp
  src/output/YdotoolDelivery.cpp
  src/output/YdotoolSetup.cpp
  src/platform/AtSpiTargetProvider.cpp
  src/platform/KGlobalAccelShortcutBinder.cpp
  src/platform/KWinActiveWindow.cpp
  src/platform/LinuxSessionKeyGrab.cpp
  src/platform/KeywatchSetup.cpp
  src/platform/KeywatchShortcutBinder.cpp
  src/platform/LinuxDesktopIntegration.cpp
  src/platform/MediaPauseController.cpp
  src/platform/PortalScreenshotContextProvider.cpp
  src/platform/PortalGlobalShortcutBinder.cpp
  src/platform/KWinBlurBackdrop.cpp
  src/platform/WaylandLayerShell.cpp
  src/platform/X11KeyGrabShortcutBinder.cpp
  src/platform/XInput2ShortcutBinder.cpp
  src/platform/atspi/AtSpiAccess.cpp
  src/platform/atspi/AtSpiCorrectionObserver.cpp
  src/platform/atspi/AtSpiTargetSnapshot.cpp
)

set(SPEECHER_PLATFORM_LINUX_HEADERS
  src/output/HelperInstall.h
  src/output/HelperPath.h
  src/output/WaylandClipboardOwner.h
  src/output/WaylandClipboardProcess.h
  src/output/WlClipboardDelivery.h
  src/output/YdotoolDelivery.h
  src/output/YdotoolSetup.h
  src/platform/AtSpiTargetProvider.h
  src/platform/KGlobalAccelShortcutBinder.h
  src/platform/KWinActiveWindow.h
  src/platform/LinuxSessionKeyGrab.h
  src/platform/KeywatchSetup.h
  src/platform/KeywatchShortcutBinder.h
  src/platform/LinuxDesktopIntegration.h
  src/platform/MediaPauseController.h
  src/platform/PortalScreenshotContextProvider.h
  src/platform/PortalGlobalShortcutBinder.h
  src/platform/KWinBlurBackdrop.h
  src/platform/WaylandLayerShell.h
  src/platform/X11KeyGrabShortcutBinder.h
  src/platform/XInput2ShortcutBinder.h
  src/setup/KeywatchProtocol.h
  src/platform/atspi/AtSpiAccess.h
  src/platform/atspi/AtSpiCorrectionObserver.h
  src/platform/atspi/AtSpiTargetSnapshot.h
)

set(SPEECHER_PLATFORM_MAC_SOURCES
  src/output/mac/MacPasteDelivery.mm
  src/platform/mac/MacSessionKeyGrab.mm
  src/platform/mac/MacCorrectionObserver.mm
  src/platform/mac/MacGlobalShortcutBinder.mm
  src/platform/mac/MacKeyCode.mm
  src/platform/mac/MacMediaController.cpp
  src/platform/mac/MacClickThrough.mm
  src/platform/mac/MacMediaController.mm
  src/platform/mac/MacPopupPositioner.mm
  src/platform/mac/MacOpenRequests.mm
  src/platform/mac/MacWindowBackdrop.mm
  src/platform/mac/MacScreenshotContextProvider.mm
  src/platform/mac/MacSingleKeyShortcutBinder.mm
  src/platform/mac/MacSystemAudioInput.mm
  src/platform/mac/MacTargetProvider.mm
)

set(SPEECHER_PLATFORM_MAC_HEADERS
  src/output/mac/MacPasteDelivery.h
  src/platform/mac/MacSessionKeyGrab.h
  src/platform/mac/MacCorrectionObserver.h
  src/platform/mac/MacGlobalShortcutBinder.h
  src/platform/mac/MacClickThrough.h
  src/platform/mac/MacKeyCode.h
  src/platform/mac/MacMediaController.h
  src/platform/mac/MacPopupPositioner.h
  src/platform/mac/MacOpenRequests.h
  src/platform/mac/MacWindowBackdrop.h
  src/platform/mac/MacScreenshotContextProvider.h
  src/platform/mac/MacSingleKeyShortcutBinder.h
  src/platform/mac/MacSystemAudioCapture.h
  src/platform/mac/MacSystemAudioInput.h
  src/platform/mac/MacTargetProvider.h
)

set(SPEECHER_PLATFORM_WIN_SOURCES
  src/output/win/WinPasteDelivery.cpp
  src/platform/win/WinSessionKeyGrab.cpp
  src/platform/win/WinCaptureInput.cpp
  src/platform/win/WinCommunicationsAudioInput.cpp
  src/platform/win/WinCorrectionObserver.cpp
  src/platform/win/WinGlobalShortcutBinder.cpp
  src/platform/win/WinMediaController.cpp
  src/platform/win/WinRawKeyboard.cpp
  src/platform/win/WinScreenshotContextProvider.cpp
  src/platform/win/WinBroadcasts.cpp
  src/platform/win/WinSingleKeyShortcutBinder.cpp
  src/platform/win/WinSystemAudioInput.cpp
  src/platform/win/WinTargetProvider.cpp
  src/platform/win/WinWindowBackdrop.cpp
)
set(SPEECHER_PLATFORM_WIN_HEADERS
  src/output/win/WinPasteDelivery.h
  src/platform/win/WinSessionKeyGrab.h
  src/platform/win/WinCaptureInput.h
  src/platform/win/WinCommunicationsAudioInput.h
  src/platform/win/WinCorrectionObserver.h
  src/platform/win/WinGlobalShortcutBinder.h
  src/platform/win/WinInjectedInput.h
  src/platform/win/WinMediaController.h
  src/platform/win/WinRawKeyboard.h
  src/platform/win/WinScreenshotContextProvider.h
  src/platform/win/WinBroadcasts.h
  src/platform/win/WinSingleKeyShortcutBinder.h
  src/platform/win/WinSystemAudioInput.h
  src/platform/win/WinTargetProvider.h
  src/platform/win/WinWindowBackdrop.h
)

set(SPEECHER_APP_SOURCES
  src/app/AccessibilityMonitor.cpp
  src/app/ApplicationController.cpp
  src/app/LocalSetup.cpp
  src/app/NetworkReachability.cpp
  src/app/ProviderAvailability.cpp
  src/app/CommandLine.cpp
  src/app/CompositionEchoCanceller.cpp
  src/app/CompositionSockets.cpp
  src/app/HeadlessTranscribe.cpp
  src/app/IpcCommandHandler.cpp
  src/app/IpcMessages.cpp
  src/app/ManifestUpdater.cpp
  src/app/MicrophoneTest.cpp
  src/app/PlatformSelection.cpp
  src/app/PhoneTransfer.cpp
  src/app/ProviderSetup.cpp
  src/app/ProvidersCommand.cpp
  src/app/ShellCompletions.cpp
  src/app/ShortcutGesture.cpp
  src/app/SingleInstanceIpc.cpp
  src/app/WhatsNewTracker.cpp
  src/recording/RecordingPresentation.cpp
  src/recording/RecordingSession.cpp
  src/recording/RecordingStatus.cpp
  src/transcribe/FileTranscriptionSession.cpp
  src/transcribe/Subtitles.cpp
  src/transcribe/TranscribePresentation.cpp
  src/transcribe/TranscribeQueue.cpp
)

set(SPEECHER_APP_HEADERS
  src/app/AccessibilityMonitor.h
  src/app/AppFrontEnd.h
  src/app/ApplicationController.h
  src/app/LocalSetup.h
  src/app/NetworkReachability.h
  src/app/ProviderAvailability.h
  src/app/MicrophoneTest.h
  src/app/CommandLine.h
  src/app/CompositionEchoCanceller.h
  src/app/CompositionSockets.h
  src/app/HeadlessTranscribe.h
  src/app/IpcCommandHandler.h
  src/app/IpcMessages.h
  src/app/ManifestUpdater.h
  src/app/ShortcutGesture.h
  src/app/ShortcutSuspendingDelivery.h
  src/app/PlatformComposition.h
  src/app/PhoneTransfer.h
  src/app/ProviderSetup.h
  src/app/ProvidersCommand.h
  src/app/ShellCompletions.h
  src/app/SingleInstanceIpc.h
  src/app/UpdateController.h
  src/app/WhatsNewTracker.h
  src/recording/RecordingPresentation.h
  src/recording/RecordingSession.h
  src/recording/RecordingStatus.h
  src/transcribe/FileTranscriptionSession.h
  src/transcribe/Subtitles.h
  src/transcribe/TranscribePresentation.h
  src/transcribe/TranscribeQueue.h
)

set(SPEECHER_APP_LINUX_SOURCES
  src/app/LinuxComposition.cpp
  src/app/SpeecherRemoval.cpp
  src/app/VirtualKeyboardSetup.cpp
)

set(SPEECHER_APP_LINUX_HEADERS
  src/app/LinuxComposition.h
  src/app/SpeecherRemoval.h
  src/app/VirtualKeyboardSetup.h
)

set(SPEECHER_APP_WIN_SOURCES
  src/app/WindowsComposition.cpp
  src/app/WindowsInstallerUpdater.cpp
)

set(SPEECHER_APP_WIN_HEADERS
  src/app/WindowsComposition.h
  src/app/WindowsInstallerUpdater.h
)

set(SPEECHER_APP_MAC_SOURCES
  src/app/MacComposition.mm
  src/app/MacSparkleUpdater.mm
)

set(SPEECHER_APP_MAC_HEADERS
  src/app/MacComposition.h
  src/app/MacSparkleUpdater.h
)

add_library(speecher_domain STATIC
  ${SPEECHER_DOMAIN_SOURCES}
  ${SPEECHER_DOMAIN_HEADERS}
)
file(GLOB SPEECHER_RELEASE_NOTES CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/docs/releases/[0-9]*.md"
)
qt_add_resources(speecher_domain release_notes
  PREFIX "/releases"
  BASE "${CMAKE_CURRENT_SOURCE_DIR}/docs/releases"
  FILES ${SPEECHER_RELEASE_NOTES}
)
qt_add_resources(speecher_domain release_history
  PREFIX "/releases"
  BASE "${SPEECHER_RELEASE_HISTORY_DIR}"
  FILES "${SPEECHER_RELEASE_HISTORY_DIR}/history.log"
)
target_include_directories(speecher_domain PUBLIC src)
# Gui for QKeySequence, which ShortcutBinding carries for key combinations.
target_link_libraries(speecher_domain PUBLIC Qt6::Core Qt6::Gui)

add_library(speecher_workflow STATIC
  ${SPEECHER_WORKFLOW_SOURCES}
  ${SPEECHER_WORKFLOW_HEADERS}
)
target_include_directories(speecher_workflow PUBLIC src)
target_link_libraries(speecher_workflow PUBLIC speecher_domain)

add_library(speecher_providers STATIC
  ${SPEECHER_PROVIDER_SOURCES}
  ${SPEECHER_PROVIDER_HEADERS}
)
target_include_directories(speecher_providers PUBLIC src)
target_link_libraries(speecher_providers PUBLIC speecher_workflow Qt6::Network)
if(APPLE)
  target_link_libraries(speecher_providers PRIVATE "-framework Security")
elseif(WIN32)
  target_link_libraries(speecher_providers PRIVATE Advapi32)
endif()

# Local speech runs in-process on transcribe.cpp; see
# docs/adr/0006-local-models-and-custom-endpoints.md. Linux and Windows x64
# build the ggml backends as modules shipped beside libtranscribe (one module
# per CPU instruction set, plus Vulkan), so one package runs on any x86-64
# machine and falls back to the CPU where no Vulkan driver loads. Elsewhere
# (macOS, ARM) everything is static, with Metal compiled in on Apple Silicon.
#
# Off by default so an everyday build stays fast: transcribe.cpp more than
# doubles a clean build. Release builds turn it on. A local build with it on
# compiles one CPU backend tuned for this machine unless
# SPEECHER_LOCAL_SPEECH_ALL_CPUS asks for every instruction set, as packages do.
option(SPEECHER_WITH_LOCAL_SPEECH "Run speech models on this computer with transcribe.cpp"
  ${SPEECHER_RELEASE_BUILD})
if(SPEECHER_WITH_LOCAL_SPEECH)
  set(SPEECHER_LOCAL_SPEECH_MODULES OFF)
  if(NOT APPLE AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64)$")
    set(SPEECHER_LOCAL_SPEECH_MODULES ON)
  endif()

  set(SPEECHER_LOCAL_SPEECH_VULKAN_FOUND OFF)
  if(SPEECHER_LOCAL_SPEECH_MODULES)
    # Where the LunarG SDK installs, as ggml looks too.
    if(DEFINED ENV{VULKAN_SDK})
      list(APPEND CMAKE_PREFIX_PATH "$ENV{VULKAN_SDK}")
    endif()
    find_package(Vulkan QUIET COMPONENTS glslc)
    find_package(SPIRV-Headers CONFIG QUIET)
    if(Vulkan_FOUND AND Vulkan_glslc_FOUND AND SPIRV-Headers_FOUND)
      set(SPEECHER_LOCAL_SPEECH_VULKAN_FOUND ON)
    endif()
  endif()
  # Release builds must not quietly drop to the CPU: the Linux release profile
  # always wants Vulkan, and the Windows release job asks for it explicitly.
  set(SPEECHER_LOCAL_SPEECH_VULKAN_DEFAULT ${SPEECHER_LOCAL_SPEECH_VULKAN_FOUND})
  if(SPEECHER_RELEASE_BUILD)
    set(SPEECHER_LOCAL_SPEECH_VULKAN_DEFAULT ON)
  endif()
  option(SPEECHER_LOCAL_SPEECH_VULKAN "Build transcribe.cpp's Vulkan backend"
    ${SPEECHER_LOCAL_SPEECH_VULKAN_DEFAULT})
  if(SPEECHER_LOCAL_SPEECH_VULKAN AND NOT SPEECHER_LOCAL_SPEECH_VULKAN_FOUND)
    message(FATAL_ERROR "SPEECHER_LOCAL_SPEECH_VULKAN needs the Vulkan headers, glslc and "
      "SPIRV-Headers (Debian: libvulkan-dev glslc spirv-headers; Windows: the LunarG "
      "Vulkan SDK plus SPIRV-Headers on CMAKE_PREFIX_PATH).")
  endif()
  if(NOT SPEECHER_LOCAL_SPEECH_VULKAN)
    message(STATUS "Local speech: Vulkan is off; models will run on the CPU.")
  endif()
  # CUDA and ROCm are extra modules beside Vulkan for a machine that has the
  # toolkit. Packages leave them off: they would need the CUDA or ROCm runtime
  # libraries, which Speecher does not ship, and the module fails to load
  # without them, leaving Vulkan and the CPU.
  option(SPEECHER_LOCAL_SPEECH_CUDA "Build transcribe.cpp's CUDA backend (needs the CUDA toolkit)" OFF)
  option(SPEECHER_LOCAL_SPEECH_ROCM "Build transcribe.cpp's ROCm backend (needs ROCm 6.1 or newer)" OFF)
  if((SPEECHER_LOCAL_SPEECH_CUDA OR SPEECHER_LOCAL_SPEECH_ROCM) AND NOT SPEECHER_LOCAL_SPEECH_MODULES)
    message(FATAL_ERROR "SPEECHER_LOCAL_SPEECH_CUDA and SPEECHER_LOCAL_SPEECH_ROCM build loadable "
      "modules, which only x86-64 Linux and Windows builds use.")
  endif()

  set(TRANSCRIBE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(TRANSCRIBE_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(TRANSCRIBE_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
  # A package must not depend on whichever BLAS the build host has installed.
  set(TRANSCRIBE_USE_SYSTEM_BLAS OFF CACHE BOOL "" FORCE)
  set(TRANSCRIBE_VULKAN ${SPEECHER_LOCAL_SPEECH_VULKAN} CACHE BOOL "" FORCE)
  set(TRANSCRIBE_CUDA ${SPEECHER_LOCAL_SPEECH_CUDA} CACHE BOOL "" FORCE)
  set(TRANSCRIBE_HIP ${SPEECHER_LOCAL_SPEECH_ROCM} CACHE BOOL "" FORCE)
  set(TRANSCRIBE_BUILD_SHARED ${SPEECHER_LOCAL_SPEECH_MODULES} CACHE BOOL "" FORCE)
  set(TRANSCRIBE_GGML_BACKEND_DL ${SPEECHER_LOCAL_SPEECH_MODULES} CACHE BOOL "" FORCE)
  # Packages carry every CPU backend so they run on any x86-64 machine; a
  # local build compiles only the one tuned for the machine it runs on.
  option(SPEECHER_LOCAL_SPEECH_ALL_CPUS
    "Build a CPU backend for every x86-64 instruction set, as release packages need"
    ${SPEECHER_RELEASE_BUILD})
  if(SPEECHER_LOCAL_SPEECH_MODULES AND SPEECHER_LOCAL_SPEECH_ALL_CPUS)
    set(GGML_CPU_ALL_VARIANTS ON CACHE BOOL "" FORCE)
    set(GGML_NATIVE OFF CACHE BOOL "" FORCE)
  else()
    set(GGML_CPU_ALL_VARIANTS OFF CACHE BOOL "" FORCE)
    set(GGML_NATIVE ${SPEECHER_LOCAL_SPEECH_MODULES} CACHE BOOL "" FORCE)
  endif()

  include(FetchContent)
  # v0.2.4's release commit.
  FetchContent_Declare(transcribe
    URL https://github.com/handy-computer/transcribe.cpp/archive/7d37cea2248a1fb6aca9652a1d37debccbbb1ff3.tar.gz
    URL_HASH SHA256=abbe8a1e7352cc2a101ffdc37b2a0de1e6b3f04121e27d24ff1d63821ab6ccfb
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    # Added below with EXCLUDE_FROM_ALL, which keeps ggml's headers and CMake
    # package files out of `cmake --install`.
    SOURCE_SUBDIR speecher-adds-this-itself
  )
  FetchContent_MakeAvailable(transcribe)
  # A function scope so that Qt's automatic code generation, switched on for
  # this project, stays off for transcribe.cpp's plain C and C++ targets.
  function(speecher_add_transcribe)
    set(CMAKE_AUTOMOC OFF)
    set(CMAKE_AUTORCC OFF)
    set(CMAKE_AUTOUIC OFF)
    add_subdirectory("${transcribe_SOURCE_DIR}" "${transcribe_BINARY_DIR}" EXCLUDE_FROM_ALL)
  endfunction()
  speecher_add_transcribe()
  # transcribe.cpp names its headers from CMAKE_SOURCE_DIR, which is Speecher's
  # tree when it is built as a subproject.
  target_include_directories(transcribe PUBLIC "$<BUILD_INTERFACE:${transcribe_SOURCE_DIR}/include>")
  # transcribe.cpp and the ggml and miniz code it vendors are MIT licensed;
  # their notices ship beside the binaries on every platform.
  set(SPEECHER_LOCAL_SPEECH_NOTICES "${CMAKE_CURRENT_BINARY_DIR}/transcribe.cpp-licenses.md")
  file(READ "${transcribe_SOURCE_DIR}/LICENSE" _transcribe_license)
  file(READ "${transcribe_SOURCE_DIR}/THIRD-PARTY-LICENSES.md" _transcribe_third_party)
  file(WRITE "${SPEECHER_LOCAL_SPEECH_NOTICES}"
    "# transcribe.cpp\n\n${_transcribe_license}\n\n${_transcribe_third_party}")

  if(SPEECHER_LOCAL_SPEECH_MODULES)
    # libtranscribe finds its backend modules in its own directory, so keep
    # every piece beside the executables in the build tree and beside
    # libtranscribe once installed.
    set(SPEECHER_LOCAL_SPEECH_LIBRARIES transcribe ggml ggml-base)
    # ggml only ever adds to this list, so a backend switched off since the
    # last configure can still be named in it.
    foreach(backend IN LISTS GGML_AVAILABLE_BACKENDS)
      if(TARGET ${backend})
        list(APPEND SPEECHER_LOCAL_SPEECH_LIBRARIES ${backend})
      endif()
    endforeach()
    # The package carries the libraries, not the headers to build against them.
    set_target_properties(ggml PROPERTIES PUBLIC_HEADER "")
    set_target_properties(${SPEECHER_LOCAL_SPEECH_LIBRARIES} PROPERTIES
      LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
      RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    )
    include(GNUInstallDirs)
    if(WIN32)
      set(SPEECHER_LOCAL_SPEECH_INSTALL_DIR "${CMAKE_INSTALL_BINDIR}")
    else()
      set(SPEECHER_LOCAL_SPEECH_INSTALL_DIR "${CMAKE_INSTALL_LIBDIR}")
    endif()
    install(TARGETS ${SPEECHER_LOCAL_SPEECH_LIBRARIES}
      LIBRARY DESTINATION "${SPEECHER_LOCAL_SPEECH_INSTALL_DIR}" NAMELINK_SKIP
      RUNTIME DESTINATION "${SPEECHER_LOCAL_SPEECH_INSTALL_DIR}"
    )
  endif()

  target_sources(speecher_providers PRIVATE
    src/providers/HardwareProbe.cpp
    src/providers/HardwareProbe.h
    src/providers/LocalSpeechEngine.cpp
    src/providers/LocalSpeechEngine.h
    src/providers/LocalSpeechTranscriber.cpp
    src/providers/LocalSpeechTranscriber.h
  )
  target_compile_definitions(speecher_providers PUBLIC SPEECHER_WITH_LOCAL_SPEECH=1)
  target_link_libraries(speecher_providers PRIVATE transcribe)
  # LibriSpeech test-clean utterance 6930-75918-0018 (CC BY 4.0, Panayotov et
  # al., openslr.org/12), 16 kHz mono signed 16-bit little-endian PCM.
  qt_add_resources(speecher_providers speed_test_clip
    PREFIX "/speedtest"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}/assets/speedtest"
    FILES "${CMAKE_CURRENT_SOURCE_DIR}/assets/speedtest/librispeech-6930-75918-0018.s16le"
  )
endif()

add_library(speecher_platform STATIC
  ${SPEECHER_PLATFORM_SOURCES}
  ${SPEECHER_PLATFORM_HEADERS}
)
target_include_directories(speecher_platform PUBLIC src)
# Gui, not Widgets: the popup port speaks PopupSurface, so the Qt Quick popup
# can be positioned through it.
target_link_libraries(speecher_platform PUBLIC speecher_workflow Qt6::Gui Qt6::Multimedia)
if(UNIX AND NOT APPLE)
  target_sources(speecher_platform PRIVATE
    ${SPEECHER_PLATFORM_LINUX_SOURCES}
    ${SPEECHER_PLATFORM_LINUX_HEADERS}
  )
  # Public: ClipboardDelivery's members and TextDelivery's method order differ
  # with the Wayland and ydotool backends compiled in.
  target_compile_definitions(speecher_platform PUBLIC SPEECHER_WITH_WAYLAND=1)
  # Network: the Wayland key-watch binder talks to the daemon over QLocalSocket.
  # util: forkpty for the pkexec authentication conversation (in libc since
  # glibc 2.34; the stub keeps older links working).
  target_link_libraries(speecher_platform PUBLIC Qt6::DBus Qt6::Network util)
  # X11/Xi/Xext: the XInput2 single-key watcher and the popup's input shape,
  # only where the dev files exist.
  if(X11_FOUND AND XI_FOUND AND XEXT_FOUND)
    target_compile_definitions(speecher_platform PUBLIC SPEECHER_WITH_X11=1)
    target_link_libraries(speecher_platform PUBLIC PkgConfig::X11 PkgConfig::XI PkgConfig::XEXT)
  endif()
  # libpulse: system audio capture, only where the dev files exist.
  if(PULSE_FOUND)
    target_sources(speecher_platform PRIVATE
      src/platform/audio/PulseSystemAudioInput.cpp
      src/platform/audio/PulseSystemAudioInput.h
    )
    target_compile_definitions(speecher_platform PUBLIC SPEECHER_WITH_PULSE=1)
    target_link_libraries(speecher_platform PUBLIC PkgConfig::PULSE)
  endif()
  # The compositor's blur behind the translucent sidebar: the
  # ext_background_effect and older org_kde_kwin_blur protocols, made from the
  # vendored descriptions, on the window's wl_surface, which only Qt's
  # private platform interface hands out.
  find_program(WAYLAND_SCANNER_EXECUTABLE wayland-scanner)
  if(SPEECHER_RELEASE_BUILD)
    find_package(Qt6 REQUIRED COMPONENTS GuiPrivate)
  else()
    find_package(Qt6 QUIET COMPONENTS GuiPrivate)
  endif()
  if(WAYLAND_CLIENT_FOUND AND WAYLAND_SCANNER_EXECUTABLE AND TARGET Qt6::GuiPrivate)
    foreach(protocol IN ITEMS ext-background-effect-v1 kde-blur)
      set(protocol_xml "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/protocols/${protocol}.xml")
      set(protocol_header "${CMAKE_CURRENT_BINARY_DIR}/${protocol}-client-protocol.h")
      set(protocol_code "${CMAKE_CURRENT_BINARY_DIR}/${protocol}-protocol.c")
      add_custom_command(
        OUTPUT "${protocol_header}" "${protocol_code}"
        COMMAND "${WAYLAND_SCANNER_EXECUTABLE}" client-header "${protocol_xml}" "${protocol_header}"
        COMMAND "${WAYLAND_SCANNER_EXECUTABLE}" private-code "${protocol_xml}" "${protocol_code}"
        DEPENDS "${protocol_xml}"
        VERBATIM
      )
      target_sources(speecher_platform PRIVATE "${protocol_header}" "${protocol_code}")
    endforeach()
    target_include_directories(speecher_platform PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
    target_compile_definitions(speecher_platform PRIVATE SPEECHER_WITH_KWIN_BLUR=1)
    target_link_libraries(speecher_platform PRIVATE Qt6::GuiPrivate PkgConfig::WAYLAND_CLIENT)
    set(SPEECHER_KWIN_BLUR_FOUND TRUE)
  elseif(SPEECHER_RELEASE_BUILD)
    message(FATAL_ERROR "SPEECHER_RELEASE_BUILD requires wayland-client and wayland-scanner for the compositor's blur.")
  endif()
endif()
# webrtc-audio-processing 1.x: echo cancellation on Linux and macOS, only
# where the dev files exist.
if(WEBRTC_APM_FOUND AND NOT WIN32)
  target_sources(speecher_platform PRIVATE
    src/platform/audio/WebRtcEchoCanceller.cpp
    src/platform/audio/WebRtcEchoCanceller.h
  )
  target_compile_definitions(speecher_platform PUBLIC SPEECHER_WITH_WEBRTC_AEC=1)
  target_link_libraries(speecher_platform PUBLIC PkgConfig::WEBRTC_APM)
endif()
if(APPLE)
  target_sources(speecher_platform PRIVATE
    ${SPEECHER_PLATFORM_MAC_SOURCES}
    ${SPEECHER_PLATFORM_MAC_HEADERS}
  )
  # Public: TextDelivery's method order and backend set differ with the macOS
  # keyboard-paste backend compiled in.
  target_compile_definitions(speecher_platform PUBLIC SPEECHER_WITH_MAC=1)
  # ARC: the system audio input holds ScreenCaptureKit objects in C++ members
  # and hands blocks to its capture queue.
  set_source_files_properties(src/platform/mac/MacSystemAudioInput.mm src/platform/mac/MacWindowBackdrop.mm PROPERTIES
    COMPILE_OPTIONS -fobjc-arc
  )
  target_link_libraries(speecher_platform PUBLIC
    "-framework AppKit"
    "-framework Carbon"
    "-framework ApplicationServices"
    "-framework CoreMedia"
    "-framework ScreenCaptureKit"
  )
endif()
if(WIN32)
  target_sources(speecher_platform PRIVATE
    ${SPEECHER_PLATFORM_WIN_SOURCES}
    ${SPEECHER_PLATFORM_WIN_HEADERS}
  )
  target_compile_definitions(speecher_platform PUBLIC SPEECHER_WITH_WIN=1)
  target_compile_definitions(speecher_platform PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
  target_link_libraries(speecher_platform PUBLIC
    uiautomationcore user32 ole32 shell32 gdi32 windowsapp dwmapi
  )
endif()
if(ATSPI_FOUND)
  target_compile_definitions(speecher_platform PUBLIC SPEECHER_WITH_ATSPI=1)
  target_link_libraries(speecher_platform PUBLIC PkgConfig::ATSPI)
endif()
if(KF6GlobalAccel_FOUND)
  target_compile_definitions(speecher_platform PRIVATE SPEECHER_WITH_KGLOBALACCEL=1)
  target_link_libraries(speecher_platform PRIVATE KF6::GlobalAccel)
endif()

# The phone transfer's QR code and cipher (docs/adr/0008-phone-transfer-over-the-lan.md).
# Both are single files; Qt has neither.
include(FetchContent)
FetchContent_Declare(qrcodegen
  URL https://github.com/nayuki/QR-Code-generator/archive/refs/tags/v1.8.0.tar.gz
  URL_HASH SHA256=2ec0a4d33d6f521c942eeaf473d42d5fe139abcfa57d2beffe10c5cf7d34ae60
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  SOURCE_SUBDIR speecher-adds-this-itself
)
FetchContent_Declare(monocypher
  URL https://github.com/LoupVaillant/Monocypher/archive/refs/tags/4.0.3.tar.gz
  URL_HASH SHA256=a7cbae546fbdc489bca632c3747e1ceb8ca3d4bd39e2706a0916f28ccd280e50
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  SOURCE_SUBDIR speecher-adds-this-itself
)
FetchContent_MakeAvailable(qrcodegen monocypher)
add_library(speecher_qrcodegen STATIC "${qrcodegen_SOURCE_DIR}/cpp/qrcodegen.cpp")
target_include_directories(speecher_qrcodegen SYSTEM PUBLIC "${qrcodegen_SOURCE_DIR}/cpp")
add_library(speecher_monocypher STATIC "${monocypher_SOURCE_DIR}/src/monocypher.c")
target_include_directories(speecher_monocypher SYSTEM PUBLIC "${monocypher_SOURCE_DIR}/src")

# The application: sessions, permissions and single-instance IPC, with no user
# interface of its own; see docs/adr/0009-one-quick-ui-inside-the-daemon.md.
add_library(speecher_app STATIC
  ${SPEECHER_APP_SOURCES}
  ${SPEECHER_APP_HEADERS}
)
target_include_directories(speecher_app PUBLIC src)
target_compile_definitions(speecher_app PUBLIC
  SPEECHER_VERSION="${SPEECHER_VERSION}"
  SPEECHER_BUILD_NUMBER=${SPEECHER_BUILD_NUMBER}
)
target_link_libraries(speecher_app PUBLIC
  speecher_domain speecher_workflow speecher_providers speecher_platform Qt6::Network
)
target_link_libraries(speecher_app PRIVATE speecher_qrcodegen speecher_monocypher)
if(UNIX)
  target_sources(speecher_app PRIVATE
    src/app/AppImageUpdater.cpp
    src/app/AppImageUpdater.h
  )
endif()
if(UNIX AND NOT APPLE)
  target_sources(speecher_app PRIVATE
    ${SPEECHER_APP_LINUX_SOURCES}
    ${SPEECHER_APP_LINUX_HEADERS}
  )
endif()
if(APPLE)
  target_sources(speecher_app PRIVATE
    ${SPEECHER_APP_MAC_SOURCES}
    ${SPEECHER_APP_MAC_HEADERS}
  )
  set_source_files_properties(src/app/MacSparkleUpdater.mm PROPERTIES
    COMPILE_OPTIONS -fobjc-arc
  )
  target_link_libraries(speecher_app PRIVATE
    Sparkle::Sparkle
    "-framework AVFoundation"
    "-framework CoreAudio"
    "-framework Security"
    "-framework ServiceManagement"
  )
endif()
if(WIN32)
  target_sources(speecher_app PRIVATE
    ${SPEECHER_APP_WIN_SOURCES}
    ${SPEECHER_APP_WIN_HEADERS}
  )
endif()

qt_add_executable(speecher
  src/app/main.cpp
)

if(APPLE)
  # Darwin permission backends are static opt-in Qt plugins. Without this,
  # QMicrophonePermission cannot check or request the microphone grant.
  qt_import_plugins(speecher INCLUDE Qt6::QDarwinMicrophonePermissionPlugin)
endif()

target_include_directories(speecher PRIVATE src)
target_compile_definitions(speecher PRIVATE
  SPEECHER_VERSION="${SPEECHER_VERSION}"
  SPEECHER_BUILD_NUMBER=${SPEECHER_BUILD_NUMBER}
)
target_link_libraries(speecher PRIVATE speecher_app)
# The tray icon (the menu bar item on macOS) is a Qt Widgets QSystemTrayIcon.
# A library of its own so the tests link the same build of it.
add_library(speecher_tray STATIC src/app/TrayIcon.cpp src/app/TrayIcon.h)
target_link_libraries(speecher_tray PUBLIC speecher_app Qt6::Widgets)
# Speecher's own tray icons where the desktop has no icon theme: a template
# image for the menu bar, and one for each taskbar theme.
if(APPLE)
  qt_add_resources(speecher_tray speecher_tray_icons
    PREFIX /speecher/tray
    BASE packaging/macos
    FILES packaging/macos/speecher-menubar.png packaging/macos/speecher-menubar@2x.png
  )
elseif(WIN32)
  qt_add_resources(speecher_tray speecher_tray_icons
    PREFIX /speecher/tray
    BASE packaging/windows
    FILES packaging/windows/tray-dark.ico packaging/windows/tray-light.ico
  )
endif()
target_link_libraries(speecher PRIVATE speecher_tray)
include(cmake/QuickUi.cmake)

if(WIN32)
  target_sources(speecher PRIVATE packaging/windows/speecher.manifest packaging/windows/speecher.rc)
  set_target_properties(speecher PROPERTIES WIN32_EXECUTABLE TRUE)
  # The console launcher for the command line, which cmd and PowerShell wait
  # for. Plain Win32, no Qt. The installer ships it as speecher.com.
  add_executable(speecher-console src/helpers/ConsoleLauncher.cpp)
  target_include_directories(speecher-console PRIVATE src)
  set_target_properties(speecher-console PROPERTIES
    AUTOMOC OFF
    AUTORCC OFF
    AUTOUIC OFF
  )
endif()

# The licenses of the fonts the UI carries in the binary. The Windows
# installer copies the same two files.
set(SPEECHER_QUICK_FONT_LICENSES
  src/frontend/quick/assets/fonts/LICENSE-AtkinsonHyperlegibleNext.txt
  src/frontend/quick/assets/icons/LICENSE-Lucide.txt
)

if(APPLE)
  find_program(SPEECHER_DITTO_EXECUTABLE ditto REQUIRED)
  set(SPEECHER_MACOS_RESOURCES
    packaging/macos/speecher.icns
    ${SPEECHER_QUICK_FONT_LICENSES}
  )
  if(SPEECHER_WITH_LOCAL_SPEECH)
    list(APPEND SPEECHER_MACOS_RESOURCES "${SPEECHER_LOCAL_SPEECH_NOTICES}")
  endif()
  # build-webrtc-audio-processing.sh compiles Abseil into the library.
  if(WEBRTC_APM_FOUND)
    list(APPEND SPEECHER_MACOS_RESOURCES
      packaging/webrtc-audio-processing-licenses.txt
      packaging/macos/abseil-cpp-license.txt
    )
  endif()
  target_sources(speecher PRIVATE ${SPEECHER_MACOS_RESOURCES})
  set_source_files_properties(${SPEECHER_MACOS_RESOURCES} PROPERTIES
    MACOSX_PACKAGE_LOCATION "Resources"
  )
  set_target_properties(speecher PROPERTIES
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/packaging/macos/Info.plist.in"
    BUILD_WITH_INSTALL_RPATH TRUE
    INSTALL_RPATH "@executable_path/../Frameworks"
  )
  add_custom_command(TARGET speecher POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E make_directory
      "$<TARGET_BUNDLE_CONTENT_DIR:speecher>/Frameworks"
    COMMAND "${SPEECHER_DITTO_EXECUTABLE}"
      "${SPEECHER_SPARKLE_FRAMEWORK}"
      "$<TARGET_BUNDLE_CONTENT_DIR:speecher>/Frameworks/Sparkle.framework"
    COMMENT "Embedding Sparkle.framework"
    VERBATIM
  )
endif()

if(UNIX AND NOT APPLE)
  add_executable(speecher-ydotool-setup
    src/setup/YdotoolSetupHelper.cpp
  )
  # The privileged key-watch helpers: the pkexec'd installer and the daemon.
  # Plain C++, no Qt; the daemon needs libsystemd for its activated socket.
  add_executable(speecher-keywatchd
    src/setup/KeywatchDaemon.cpp
  )
  target_include_directories(speecher-keywatchd PRIVATE src/setup)
  # The installer looks for the daemon's SELinux module beside itself, so a
  # build-tree helper needs it there too.
  configure_file(src/setup/speecher_keywatchd.cil speecher_keywatchd.cil COPYONLY)
  set(SPEECHER_KEYWATCH_DIGESTS_HEADER
    "${CMAKE_CURRENT_BINARY_DIR}/generated/KeywatchPayloadDigests.h")
  add_custom_command(
    OUTPUT "${SPEECHER_KEYWATCH_DIGESTS_HEADER}"
    COMMAND "${CMAKE_COMMAND}"
      "-DDAEMON=$<TARGET_FILE:speecher-keywatchd>"
      "-DPOLICY=${CMAKE_CURRENT_BINARY_DIR}/speecher_keywatchd.cil"
      "-DOUTPUT=${SPEECHER_KEYWATCH_DIGESTS_HEADER}"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/GenerateKeywatchDigests.cmake"
    DEPENDS speecher-keywatchd src/setup/speecher_keywatchd.cil
      cmake/GenerateKeywatchDigests.cmake
    VERBATIM
  )
  add_executable(speecher-keywatch-setup
    src/setup/KeywatchSetupHelper.cpp
    "${SPEECHER_KEYWATCH_DIGESTS_HEADER}"
  )
  target_include_directories(speecher-keywatch-setup PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}/generated")
  pkg_check_modules(SYSTEMD REQUIRED IMPORTED_TARGET libsystemd)
  target_link_libraries(speecher-keywatchd PRIVATE PkgConfig::SYSTEMD)
  # AppImage assembly applies this RUNPATH to libexec files. Link the daemon
  # with it now so that packaging cannot change the payload after its digest
  # has been compiled into the installer.
  set_target_properties(speecher-keywatchd PROPERTIES
    BUILD_WITH_INSTALL_RPATH TRUE
    INSTALL_RPATH "$ORIGIN/../../lib"
  )
  set_target_properties(
    speecher-ydotool-setup speecher-keywatch-setup speecher-keywatchd
    PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
  )
endif()

find_program(WAYLAND_SCANNER_EXECUTABLE wayland-scanner)
set(SPEECHER_EXT_DATA_CONTROL_XML
  "${WAYLAND_PROTOCOLS_DATA_DIR}/staging/ext-data-control/ext-data-control-v1.xml"
)
if(SPEECHER_RELEASE_BUILD AND (NOT WAYLAND_SCANNER_EXECUTABLE
   OR NOT EXISTS "${SPEECHER_EXT_DATA_CONTROL_XML}"))
  message(FATAL_ERROR
    "SPEECHER_RELEASE_BUILD requires wayland-scanner and ext-data-control-v1.xml.")
endif()
if(UNIX AND NOT APPLE AND WAYLAND_CLIENT_FOUND AND WAYLAND_SCANNER_EXECUTABLE
   AND EXISTS "${SPEECHER_EXT_DATA_CONTROL_XML}")
  set(SPEECHER_EXT_DATA_CONTROL_HEADER
    "${CMAKE_CURRENT_BINARY_DIR}/ext-data-control-v1-client-protocol.h"
  )
  set(SPEECHER_EXT_DATA_CONTROL_CODE
    "${CMAKE_CURRENT_BINARY_DIR}/ext-data-control-v1-protocol.c"
  )
  add_custom_command(
    OUTPUT "${SPEECHER_EXT_DATA_CONTROL_HEADER}" "${SPEECHER_EXT_DATA_CONTROL_CODE}"
    COMMAND "${WAYLAND_SCANNER_EXECUTABLE}" client-header
      "${SPEECHER_EXT_DATA_CONTROL_XML}" "${SPEECHER_EXT_DATA_CONTROL_HEADER}"
    COMMAND "${WAYLAND_SCANNER_EXECUTABLE}" private-code
      "${SPEECHER_EXT_DATA_CONTROL_XML}" "${SPEECHER_EXT_DATA_CONTROL_CODE}"
    DEPENDS "${SPEECHER_EXT_DATA_CONTROL_XML}"
    VERBATIM
  )
  qt_add_executable(speecher-wayland-clipboard
    src/helpers/WaylandClipboardHelper.cpp
    "${SPEECHER_EXT_DATA_CONTROL_CODE}"
    "${SPEECHER_EXT_DATA_CONTROL_HEADER}"
  )
  target_include_directories(speecher-wayland-clipboard PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}"
  )
  target_link_libraries(speecher-wayland-clipboard PRIVATE
    Qt6::Core PkgConfig::WAYLAND_CLIENT
  )
endif()
if(Qt6WebSockets_FOUND)
  target_compile_definitions(speecher_providers PUBLIC SPEECHER_WITH_QT_WEBSOCKETS=1)
  target_link_libraries(speecher_providers PUBLIC Qt6::WebSockets)
endif()
if(LayerShellQt_FOUND)
  target_compile_definitions(speecher_platform PUBLIC SPEECHER_WITH_LAYER_SHELL=1)
  target_link_libraries(speecher_platform PUBLIC LayerShellQt::Interface)
  include(CheckCXXSourceCompiles)
  include(CMakePushCheckState)

  cmake_push_check_state(RESET)
  set(CMAKE_REQUIRED_LIBRARIES LayerShellQt::Interface)
  check_cxx_source_compiles("
    #include <LayerShellQt/Window>
    int main()
    {
      LayerShellQt::Window *window = nullptr;
      window->setActivateOnShow(false);
      return 0;
    }
  " SPEECHER_LAYER_SHELL_HAS_ACTIVATE_ON_SHOW)
  check_cxx_source_compiles("
    #include <LayerShellQt/Window>
    #include <QSize>
    int main()
    {
      LayerShellQt::Window *window = nullptr;
      window->setDesiredSize(QSize());
      return 0;
    }
  " SPEECHER_LAYER_SHELL_HAS_DESIRED_SIZE)
  check_cxx_source_compiles("
    #include <LayerShellQt/Window>
    #include <QScreen>
    int main()
    {
      LayerShellQt::Window *window = nullptr;
      QScreen *screen = nullptr;
      window->setScreen(screen);
      return 0;
    }
  " SPEECHER_LAYER_SHELL_HAS_WINDOW_SCREEN)
  check_cxx_source_compiles("
    #include <LayerShellQt/Window>
    int main()
    {
      LayerShellQt::Window *window = nullptr;
      window->setWantsToBeOnActiveScreen(true);
      return 0;
    }
  " SPEECHER_LAYER_SHELL_HAS_ACTIVE_SCREEN)
  cmake_pop_check_state()

  if(SPEECHER_LAYER_SHELL_HAS_ACTIVATE_ON_SHOW)
    target_compile_definitions(speecher_platform PUBLIC SPEECHER_LAYER_SHELL_HAS_ACTIVATE_ON_SHOW=1)
  endif()
  if(SPEECHER_LAYER_SHELL_HAS_DESIRED_SIZE)
    target_compile_definitions(speecher_platform PUBLIC SPEECHER_LAYER_SHELL_HAS_DESIRED_SIZE=1)
  endif()
  if(SPEECHER_LAYER_SHELL_HAS_WINDOW_SCREEN)
    target_compile_definitions(speecher_platform PUBLIC SPEECHER_LAYER_SHELL_HAS_WINDOW_SCREEN=1)
  endif()
  if(SPEECHER_LAYER_SHELL_HAS_ACTIVE_SCREEN)
    target_compile_definitions(speecher_platform PUBLIC SPEECHER_LAYER_SHELL_HAS_ACTIVE_SCREEN=1)
  endif()
endif()

if(SPEECHER_WITH_QKEYCHAIN)
  if(SPEECHER_RELEASE_BUILD)
    find_package(Qt6Keychain REQUIRED)
  else()
    find_package(Qt6Keychain QUIET)
  endif()
  if((WIN32 OR APPLE) AND NOT Qt6Keychain_FOUND)
    include(FetchContent)
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    set(BUILD_WITH_QT6 ON CACHE BOOL "" FORCE)
    set(BUILD_TEST_APPLICATION OFF CACHE BOOL "" FORCE)
    set(BUILD_TRANSLATIONS OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(qtkeychain
      GIT_REPOSITORY https://github.com/frankosterfeld/qtkeychain.git
      GIT_TAG v0.14.0
    )
    FetchContent_MakeAvailable(qtkeychain)
    target_include_directories(qt6keychain PUBLIC
      "$<BUILD_INTERFACE:${qtkeychain_SOURCE_DIR}>"
      "$<BUILD_INTERFACE:${qtkeychain_BINARY_DIR}>"
    )
    set(Qt6Keychain_FOUND TRUE)
  endif()
  if(TARGET qt6keychain)
    if(UNIX AND NOT APPLE)
      speecher_assert_package_qt_version("Qt6Keychain" "qt6keychain")
    endif()
    target_compile_definitions(speecher_domain PUBLIC SPEECHER_WITH_QKEYCHAIN=1)
    target_link_libraries(speecher_domain PUBLIC qt6keychain)
  endif()
endif()

message(STATUS "Speecher feature summary:")
message(STATUS "  release profile: ${SPEECHER_RELEASE_BUILD}")
message(STATUS "  KDE GlobalAccel: ${KF6GlobalAccel_FOUND}")
message(STATUS "  LayerShellQt: ${LayerShellQt_FOUND}")
message(STATUS "  Qt WebSockets: ${Qt6WebSockets_FOUND}")
message(STATUS "  QtKeychain: ${Qt6Keychain_FOUND}")
message(STATUS "  AT-SPI: ${ATSPI_FOUND}")
message(STATUS "  libpulse (system audio): ${PULSE_FOUND}")
message(STATUS "  webrtc-audio-processing-1 (echo cancellation): ${WEBRTC_APM_FOUND}")
message(STATUS "  Wayland helper: ${WAYLAND_CLIENT_FOUND}/${WAYLAND_SCANNER_EXECUTABLE}")
message(STATUS "  KWin blur: ${SPEECHER_KWIN_BLUR_FOUND}")

include(GNUInstallDirs)
if(NOT DEFINED SPEECHER_DESKTOP_EXEC)
  set(SPEECHER_DESKTOP_EXEC "${CMAKE_INSTALL_FULL_BINDIR}/speecher")
endif()
set(SPEECHER_YDOTOOL_HELPER_PATH "${CMAKE_INSTALL_FULL_LIBEXECDIR}/speecher/speecher-ydotool-setup")
set(SPEECHER_KEYWATCH_HELPER_PATH "${CMAKE_INSTALL_FULL_LIBEXECDIR}/speecher/speecher-keywatch-setup")
target_compile_definitions(speecher_platform PUBLIC
  SPEECHER_YDOTOOL_HELPER_PATH="${SPEECHER_YDOTOOL_HELPER_PATH}"
  SPEECHER_KEYWATCH_HELPER_PATH="${SPEECHER_KEYWATCH_HELPER_PATH}"
)
if(TARGET speecher-wayland-clipboard)
  set(SPEECHER_WAYLAND_CLIPBOARD_HELPER_PATH
    "${CMAKE_INSTALL_FULL_LIBEXECDIR}/speecher/speecher-wayland-clipboard"
  )
  target_compile_definitions(speecher_platform PUBLIC
    SPEECHER_WAYLAND_CLIPBOARD_HELPER_PATH="${SPEECHER_WAYLAND_CLIPBOARD_HELPER_PATH}"
  )
endif()
configure_file(
  packaging/io.github.firemonster612.speecher.desktop
  "${CMAKE_CURRENT_BINARY_DIR}/io.github.firemonster612.speecher.desktop"
  @ONLY
)
set(SPEECHER_INSTALL_APPLICATIONS_DIR "${CMAKE_INSTALL_DATADIR}/applications")
set(SPEECHER_INSTALL_HICOLOR_DIR "${CMAKE_INSTALL_DATADIR}/icons/hicolor")
configure_file(
  packaging/post-install.cmake.in
  "${CMAKE_CURRENT_BINARY_DIR}/post-install.cmake"
  @ONLY
)
install(TARGETS speecher
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  BUNDLE DESTINATION .
)
if(TARGET speecher-ydotool-setup)
  install(TARGETS speecher-ydotool-setup RUNTIME DESTINATION ${CMAKE_INSTALL_LIBEXECDIR}/speecher)
endif()
if(TARGET speecher-keywatch-setup)
  install(TARGETS speecher-keywatch-setup speecher-keywatchd
    RUNTIME DESTINATION ${CMAKE_INSTALL_LIBEXECDIR}/speecher)
  # The daemon's SELinux module rides beside the installer, which loads it.
  install(FILES src/setup/speecher_keywatchd.cil DESTINATION ${CMAKE_INSTALL_LIBEXECDIR}/speecher)
endif()
if(TARGET speecher-wayland-clipboard)
  install(TARGETS speecher-wayland-clipboard
    RUNTIME DESTINATION ${CMAKE_INSTALL_LIBEXECDIR}/speecher)
endif()
# The .desktop entry, hicolor icon and post-install hooks that refresh the
# desktop caches are all freedesktop-only.
if(UNIX AND NOT APPLE)
  install(FILES "${CMAKE_CURRENT_BINARY_DIR}/io.github.firemonster612.speecher.desktop" DESTINATION ${SPEECHER_INSTALL_APPLICATIONS_DIR})
  install(FILES packaging/io.github.firemonster612.speecher.svg DESTINATION ${SPEECHER_INSTALL_HICOLOR_DIR}/scalable/apps)
  if(SPEECHER_WITH_LOCAL_SPEECH)
    install(FILES "${SPEECHER_LOCAL_SPEECH_NOTICES}" DESTINATION "${CMAKE_INSTALL_DATADIR}/doc/speecher")
  endif()
  # webrtc-audio-processing is BSD-3-Clause with permissive third-party
  # parts; its Debian copyright file ships beside the binaries it links into.
  if(WEBRTC_APM_FOUND)
    install(FILES packaging/webrtc-audio-processing-licenses.txt DESTINATION "${CMAKE_INSTALL_DATADIR}/doc/speecher")
  endif()
  install(FILES ${SPEECHER_QUICK_FONT_LICENSES} DESTINATION "${CMAKE_INSTALL_DATADIR}/doc/speecher")
  install(SCRIPT "${CMAKE_CURRENT_BINARY_DIR}/post-install.cmake")
endif()

if(SPEECHER_BUILD_TESTS)
  enable_testing()
  qt_add_executable(speecher_tests
    tests/test_main.cpp
    tests/test_transcript_state.cpp
    tests/test_bindings.cpp
    tests/test_settings.cpp
    tests/test_provider_registry.cpp
    tests/test_platform_composition.cpp
    tests/test_platform_live.cpp
    tests/test_single_instance_ipc.cpp
    tests/test_delivery.cpp
    tests/test_dictation_session_lifecycle.cpp
    tests/test_dictation_session_refinement.cpp
    tests/test_dictation_session_fallbacks.cpp
    tests/test_refiners.cpp
    tests/test_vocabulary.cpp
    tests/test_phone_transfer.cpp
    tests/test_insights.cpp
    tests/test_provider_auth.cpp
    tests/test_claude_voice.cpp
    tests/test_codex_dictation.cpp
    tests/test_audio_pcm_converter.cpp
    tests/test_echo_canceller.cpp
    tests/test_local_models.cpp
    tests/test_custom_endpoints.cpp
    tests/test_file_transcription.cpp
  )
  if(UNIX)
    target_sources(speecher_tests PRIVATE tests/test_update_controller.cpp)
  endif()
  if(WIN32)
    target_sources(speecher_tests PRIVATE tests/test_win_platform.cpp)
  endif()
  if(APPLE)
    target_sources(speecher_tests PRIVATE tests/test_mac_platform.mm)
    set_source_files_properties(tests/test_mac_platform.mm PROPERTIES
      COMPILE_OPTIONS -fobjc-arc
    )
    qt_import_plugins(speecher_tests INCLUDE Qt6::QDarwinMicrophonePermissionPlugin)
  endif()
  if(NOT APPLE)
    target_sources(speecher_tests PRIVATE
      src/platform/mac/MacMediaController.cpp
      src/platform/mac/MacMediaController.h
    )
  endif()
  target_include_directories(speecher_tests PRIVATE src)
  target_compile_definitions(speecher_tests PRIVATE
    SPEECHER_VERSION="${SPEECHER_VERSION}"
    SPEECHER_BUILD_NUMBER=${SPEECHER_BUILD_NUMBER}
    SPEECHER_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
  )
  target_link_libraries(speecher_tests PRIVATE
    speecher_app speecher_tray Qt6::Widgets Qt6::Test speecher_monocypher
  )
  target_sources(speecher_tests PRIVATE
    tests/test_tray.cpp
    tests/test_quick_settings.cpp
    tests/test_quick_appearance.cpp
    tests/test_quick_dictation.cpp
    tests/test_quick_updates.cpp
    tests/test_quick_transcription.cpp
    tests/test_quick_system.cpp
    tests/test_quick_models.cpp
    tests/test_quick_insights.cpp
    tests/test_quick_accounts.cpp
    tests/test_quick_audio.cpp
    tests/test_quick_library.cpp
    tests/test_quick_pages.cpp
    tests/test_migration.cpp
    src/frontend/quick/host/QuickFrontEnd.cpp
    src/frontend/quick/host/QuickFrontEnd.h
    src/frontend/quick/host/QuickPopupWindow.cpp
    src/frontend/quick/host/QuickPopupWindow.h
  )
  # The Quick suites load the UI's QML over the C++ Backend; Qt's own
  # QML modules load from the shared Qt, as in the app.
  target_link_libraries(speecher_tests PRIVATE Qt6::Quick ${SPEECHER_QUICK_PLUGINS})
  set_target_properties(speecher_tests PROPERTIES QT_QML_MODULE_NO_IMPORT_SCAN TRUE)
  if(UNIX AND NOT APPLE)
    target_sources(speecher_tests PRIVATE
      tests/test_keywatch.cpp
      tests/test_helper_install.cpp
    )
    # The KWin blur test runs a compositor of its own.
    pkg_check_modules(WAYLAND_SERVER QUIET IMPORTED_TARGET wayland-server)
    if(SPEECHER_KWIN_BLUR_FOUND AND WAYLAND_SERVER_FOUND)
      target_compile_definitions(speecher_tests PRIVATE SPEECHER_TEST_KWIN_BLUR=1)
      target_link_libraries(speecher_tests PRIVATE PkgConfig::WAYLAND_SERVER PkgConfig::WAYLAND_CLIENT)
    endif()
  endif()
  set(SPEECHER_TEST_SUITES
    transcript_state
    bindings
    settings
    provider_registry
    platform_composition
    tray
    quick_settings
    quick_appearance
    quick_dictation
    quick_updates
    quick_transcription
    quick_system
    quick_models
    quick_insights
    quick_accounts
    quick_audio
    quick_library
    quick_pages
    migration
    platform_live
    single_instance_ipc
    delivery
    dictation_session_lifecycle
    dictation_session_refinement
    dictation_session_fallbacks
    refiners
    vocabulary
    phone_transfer
    insights
    provider_auth
    claude_voice
    codex_dictation
    audio_pcm_converter
    echo_canceller
    local_models
    custom_endpoints
    file_transcription
  )
  if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    list(APPEND SPEECHER_TEST_SUITES keywatch helper_install)
  endif()
  if(UNIX)
    list(APPEND SPEECHER_TEST_SUITES update_controller)
  endif()
  if(APPLE)
    list(APPEND SPEECHER_TEST_SUITES mac_platform)
  endif()
  if(WIN32)
    list(APPEND SPEECHER_TEST_SUITES win_platform)
  endif()
  set(SPEECHER_TEST_ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
  if(UNIX AND NOT APPLE)
    # No session bus, so no suite reaches the developer's desktop services.
    # SecretStore itself keeps tests off any keyring unless the run brings a
    # private one, as the keyring suites below do.
    list(APPEND SPEECHER_TEST_ENVIRONMENT "DBUS_SESSION_BUS_ADDRESS=unix:path=/nonexistent/speecher-tests-bus")
  endif()
  foreach(suite IN LISTS SPEECHER_TEST_SUITES)
    add_test(NAME speecher_${suite} COMMAND speecher_tests --suite ${suite})
    set_tests_properties(speecher_${suite} PROPERTIES
      ENVIRONMENT "${SPEECHER_TEST_ENVIRONMENT}" TIMEOUT 240)
  endforeach()
  # Suites run in parallel: test_main.cpp gives each process its own settings
  # and sign-in homes, and each suite its own data folders. These share what
  # the computer has one of.
  # The live checks play through the sound server and switch its default
  # output, and use the session's clipboard and accessibility bus.
  set_tests_properties(speecher_platform_live PROPERTIES RUN_SERIAL TRUE)
  # It shows work leaves the main thread by timing the event loop to within
  # 100 ms, which only a computer with nothing else running keeps to.
  set_tests_properties(speecher_custom_endpoints PROPERTIES RUN_SERIAL TRUE)
  if(APPLE OR WIN32)
    # A dictation through the real platform takes Escape and Enter, and
    # Windows' own suite more keys, as hot keys only one process at a time
    # can hold. Linux tests reach no shortcut service.
    set(SPEECHER_HOT_KEY_SUITES platform_composition quick_dictation quick_pages quick_updates)
    if(WIN32)
      list(APPEND SPEECHER_HOT_KEY_SUITES win_platform)
    endif()
    list(TRANSFORM SPEECHER_HOT_KEY_SUITES PREPEND speecher_)
    set_tests_properties(${SPEECHER_HOT_KEY_SUITES} PROPERTIES RESOURCE_LOCK hot_keys)
  endif()
  # Its drift calls play over half an hour of audio through AEC3.
  set_tests_properties(speecher_echo_canceller PROPERTIES TIMEOUT 300)
  # qmllint over the UI's QML modules. .qmllint.ini makes any warning fail it.
  # Before 6.11 qmllint mistypes list<T> properties (UpdateStatus's lines).
  if(TARGET Qt6::qmllint AND Qt6_VERSION VERSION_GREATER_EQUAL 6.11)
    add_test(NAME speecher_qmllint
      COMMAND ${CMAKE_COMMAND} --build ${CMAKE_BINARY_DIR} --target all_qmllint --config $<CONFIG>)
  endif()

  if(ATSPI_FOUND AND UNIX AND NOT APPLE)
    qt_add_executable(speecher_atspi_peer_test tests/atspi_peer_test.cpp)
    target_link_libraries(speecher_atspi_peer_test PRIVATE speecher_platform)
    add_test(NAME speecher_atspi_unavailable_peer
      COMMAND dbus-run-session -- $<TARGET_FILE:speecher_atspi_peer_test>)
    set_tests_properties(speecher_atspi_unavailable_peer PROPERTIES TIMEOUT 10)
  endif()

  # The endpoint key and migration suites also run against a working keyring,
  # which the developer's own session may not offer (a locked collection falls
  # back to the settings file). Skipped where gnome-keyring is not installed.
  # Each brings its own bus, keyring and home, so runs beside the plain suite.
  if(UNIX AND NOT APPLE)
    find_program(SPEECHER_DBUS_RUN_SESSION dbus-run-session)
    find_program(SPEECHER_GNOME_KEYRING gnome-keyring-daemon)
    if(SPEECHER_DBUS_RUN_SESSION AND SPEECHER_GNOME_KEYRING)
      add_test(NAME speecher_custom_endpoints_keyring
        COMMAND ${SPEECHER_DBUS_RUN_SESSION} -- ${CMAKE_CURRENT_SOURCE_DIR}/tests/keyring/with_session_keyring.sh
                $<TARGET_FILE:speecher_tests> --suite custom_endpoints)
      set_tests_properties(speecher_custom_endpoints_keyring PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen" RUN_SERIAL TRUE TIMEOUT 120)
      add_test(NAME speecher_migration_keyring
        COMMAND ${SPEECHER_DBUS_RUN_SESSION} -- ${CMAKE_CURRENT_SOURCE_DIR}/tests/keyring/with_session_keyring.sh
                $<TARGET_FILE:speecher_tests> --suite migration)
      set_tests_properties(speecher_migration_keyring PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 120)
    endif()
  endif()

  qt_add_executable(speecher_atspi_fixture
    tests/atspi_fixture.cpp
  )
  target_link_libraries(speecher_atspi_fixture PRIVATE Qt6::Widgets)

  # End-to-end proof for the XInput2 single-key backend. XTEST needs a real X
  # server, so this only runs where Xvfb is installed (CI's container), never
  # against the developer's live :0 session. xvfb-run -a picks a private
  # display number, well away from :0.
  if(UNIX AND NOT APPLE AND XI_FOUND)
    find_program(SPEECHER_XVFB_RUN xvfb-run)
    pkg_check_modules(XTST QUIET IMPORTED_TARGET xtst)
    if(SPEECHER_XVFB_RUN AND XTST_FOUND)
      qt_add_executable(speecher_x11_injection_test tests/x11_injection_test.cpp)
      target_include_directories(speecher_x11_injection_test PRIVATE src)
      target_link_libraries(speecher_x11_injection_test PRIVATE
        speecher_platform Qt6::Test PkgConfig::XTST PkgConfig::X11)
      add_test(NAME speecher_x11_injection
        COMMAND "${SPEECHER_XVFB_RUN}" -a $<TARGET_FILE:speecher_x11_injection_test>)
      set_tests_properties(speecher_x11_injection PROPERTIES TIMEOUT 30)
    endif()
  endif()
endif()
