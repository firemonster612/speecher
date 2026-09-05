cmake_minimum_required(VERSION 3.24)

if(CMAKE_HOST_APPLE AND NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET)
  set(CMAKE_OSX_DEPLOYMENT_TARGET "15.0" CACHE STRING
    "Minimum macOS version supported by Speecher")
endif()

project(speecher VERSION 0.1.1 LANGUAGES C CXX)

include(cmake/GitVersion.cmake)

if(APPLE)
  # The macOS platform backend is Objective-C++.
  enable_language(OBJCXX)

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
option(SPEECHER_RELEASE_BUILD "Require every Linux release integration" OFF)
set(SPEECHER_WINUI_DEFAULT OFF)
if(WIN32)
  set(SPEECHER_WINUI_DEFAULT ON)
endif()
option(SPEECHER_WITH_WINUI "Build the WinUI Windows front end"
  ${SPEECHER_WINUI_DEFAULT})
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

# The native macOS front end. Turning it off leaves the Qt front end driving the
# mac build, which is why the Swift language stays behind the option too.
set(SPEECHER_SWIFT_UI_DEFAULT OFF)
if(APPLE)
  set(SPEECHER_SWIFT_UI_DEFAULT ON)
endif()
option(SPEECHER_WITH_SWIFT_UI "Build the AppKit/SwiftUI macOS front end"
  ${SPEECHER_SWIFT_UI_DEFAULT})
if(APPLE AND SPEECHER_WITH_SWIFT_UI)
  enable_language(Swift)
endif()

if(WIN32 AND SPEECHER_WITH_WINUI)
  include(cmake/WindowsAppSdk.cmake)
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

function(speecher_assert_package_qt_version package_name target_name)
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
  if(NOT package_qt_major EQUAL Qt6_VERSION_MAJOR OR
     NOT package_qt_minor EQUAL Qt6_VERSION_MINOR)
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
  pkg_get_variable(WAYLAND_PROTOCOLS_DATA_DIR wayland-protocols pkgdatadir)
else()
  find_package(PkgConfig QUIET)
endif()
if(PkgConfig_FOUND AND NOT SPEECHER_RELEASE_BUILD)
  pkg_check_modules(ATSPI QUIET IMPORTED_TARGET atspi-2 gobject-2.0)
  pkg_check_modules(WAYLAND_CLIENT QUIET IMPORTED_TARGET wayland-client)
  pkg_get_variable(WAYLAND_PROTOCOLS_DATA_DIR wayland-protocols pkgdatadir)
endif()
if(SPEECHER_RELEASE_BUILD)
  find_package(Qt6 6.5 REQUIRED COMPONENTS WebSockets)
else()
  find_package(Qt6 6.5 QUIET COMPONENTS WebSockets)
endif()
if(UNIX AND NOT APPLE)
  find_package(Qt6 6.5 REQUIRED COMPONENTS DBus)
  if(SPEECHER_WITH_KDE)
    find_package(KF6GlobalAccel REQUIRED)
    find_package(KF6WidgetsAddons REQUIRED)
    find_package(KF6ColorScheme REQUIRED)
    find_package(LayerShellQt REQUIRED)
  else()
    find_package(KF6GlobalAccel QUIET)
    find_package(KF6WidgetsAddons QUIET)
    find_package(KF6ColorScheme QUIET)
    if(SPEECHER_WITH_LAYER_SHELL)
      find_package(LayerShellQt QUIET)
    endif()
  endif()

  speecher_assert_single_qt_prefix("KDE package discovery")
  if(KF6GlobalAccel_FOUND)
    speecher_assert_package_qt_version("KF6GlobalAccel" "KF6::GlobalAccel")
  endif()
  if(KF6WidgetsAddons_FOUND)
    speecher_assert_package_qt_version("KF6WidgetsAddons" "KF6::WidgetsAddons")
  endif()
  if(KF6ColorScheme_FOUND)
    speecher_assert_package_qt_version("KF6ColorScheme" "KF6::ColorScheme")
  endif()
  if(LayerShellQt_FOUND)
    speecher_assert_package_qt_version("LayerShellQt" "LayerShellQt::Interface")
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
  src/core/CliToolDiscovery.cpp
  src/core/LearnedCorrection.cpp
  src/core/OutputMethod.cpp
  src/core/OutputFormat.cpp
  src/core/PasteRules.cpp
  src/core/SettingsStore.cpp
  src/core/settings/CorrectionSettingsCodec.cpp
  src/core/settings/SettingsCodecs.cpp
  src/core/settings/SettingsSchema.cpp
  src/core/settings/VocabularySettingsCodec.cpp
  src/core/SecretStore.cpp
  src/core/TranscriptState.cpp
  src/core/Target.cpp
  src/core/VocabularyLimit.cpp
  src/core/Vocabulary.cpp
  src/core/WordPreview.cpp
  src/dictation/DictationTypes.cpp
)

set(SPEECHER_DOMAIN_HEADERS
  src/core/AppSettings.h
  src/core/BindingProcessor.h
  src/core/bindings/BindingMatcher.h
  src/core/bindings/NoBindDirectiveParser.h
  src/core/CliToolDiscovery.h
  src/core/LearnedCorrection.h
  src/core/OutputMethod.h
  src/core/OutputFormat.h
  src/core/PasteRules.h
  src/core/SettingsStore.h
  src/core/settings/CorrectionSettingsCodec.h
  src/core/settings/SettingsCodecs.h
  src/core/settings/SettingsKeys.h
  src/core/settings/SettingsSchema.h
  src/core/settings/VocabularySettingsCodec.h
  src/core/SecretStore.h
  src/core/TranscriptState.h
  src/core/Target.h
  src/core/VocabularyLimit.h
  src/core/Vocabulary.h
  src/core/WordPreview.h
  src/dictation/DictationTypes.h
)

set(SPEECHER_WORKFLOW_SOURCES
  src/dictation/DictationSession.cpp
  src/dictation/DictationPorts.cpp
  src/dictation/StartupPreparationRunner.cpp
  src/dictation/TranscriptPipeline.cpp
  src/providers/ProviderRegistry.cpp
)

set(SPEECHER_WORKFLOW_HEADERS
  src/dictation/DictationPorts.h
  src/dictation/DictationSession.h
  src/dictation/StartupPreparationRunner.h
  src/dictation/TranscriptPipeline.h
  src/providers/ProviderRegistry.h
)

set(SPEECHER_PROVIDER_SOURCES
  src/providers/AnthropicApiRefiner.cpp
  src/providers/AnthropicTranscriptRefiner.cpp
  src/providers/ClaudeCredentials.cpp
  src/providers/ClaudeSpeechTranscriber.cpp
  src/providers/ClaudeVoiceClient.cpp
  src/providers/ClaudeVoiceProtocol.cpp
  src/providers/CliProxyCredentials.cpp
  src/providers/CodexDictationClient.cpp
  src/providers/CodexSpeechTranscriber.cpp
  src/providers/OpenAiAuthProvider.cpp
  src/providers/OpenAiRefiner.cpp
  src/providers/OpenAiTranscriptRefiner.cpp
  src/providers/TranscriptRefinementPrompt.cpp
)

set(SPEECHER_PROVIDER_HEADERS
  src/providers/AnthropicApiRefiner.h
  src/providers/AnthropicTranscriptRefiner.h
  src/providers/ClaudeCredentials.h
  src/providers/ClaudeSpeechTranscriber.h
  src/providers/ClaudeVoiceClient.h
  src/providers/ClaudeVoiceProtocol.h
  src/providers/CliProxyCredentials.h
  src/providers/CodexDictationClient.h
  src/providers/CodexSpeechTranscriber.h
  src/providers/OpenAiAuthProvider.h
  src/providers/OpenAiRefiner.h
  src/providers/OpenAiTranscriptRefiner.h
  src/providers/TranscriptRefinementPrompt.h
)

set(SPEECHER_PLATFORM_SOURCES
  src/output/ClipboardDelivery.cpp
  src/output/QtClipboardDelivery.cpp
  src/output/TextDelivery.cpp
  src/platform/CorrectionDiff.cpp
  src/platform/ScreenshotImage.cpp
  src/platform/audio/AudioPcmConverter.cpp
  src/platform/audio/QtAudioInput.cpp
  src/platform/FallbackPopupPositioner.cpp
)

set(SPEECHER_PLATFORM_HEADERS
  src/output/ClipboardDelivery.h
  src/output/ClipboardSnapshot.h
  src/output/QtClipboardDelivery.h
  src/output/TextDelivery.h
  src/platform/AccessibilityState.h
  src/platform/CorrectionDiff.h
  src/platform/FallbackPopupPositioner.h
  src/platform/GlobalShortcutBinder.h
  src/platform/PopupPositioner.h
  src/platform/PopupSurface.h
  src/platform/ScreenshotImage.h
  src/platform/audio/AudioPcmConverter.h
  src/platform/audio/QtAudioInput.h
)

set(SPEECHER_PLATFORM_LINUX_SOURCES
  src/output/WaylandClipboardOwner.cpp
  src/output/WaylandClipboardProcess.cpp
  src/output/WlClipboardDelivery.cpp
  src/output/YdotoolDelivery.cpp
  src/output/YdotoolSetup.cpp
  src/platform/AtSpiTargetProvider.cpp
  src/platform/KGlobalAccelShortcutBinder.cpp
  src/platform/LinuxDesktopIntegration.cpp
  src/platform/LinuxStyleChoice.cpp
  src/platform/MediaPauseController.cpp
  src/platform/PortalScreenshotContextProvider.cpp
  src/platform/PortalGlobalShortcutBinder.cpp
  src/platform/WaylandLayerShell.cpp
  src/platform/atspi/AtSpiAccess.cpp
  src/platform/atspi/AtSpiCorrectionObserver.cpp
  src/platform/atspi/AtSpiTargetSnapshot.cpp
)

set(SPEECHER_PLATFORM_LINUX_HEADERS
  src/output/HelperPath.h
  src/output/WaylandClipboardOwner.h
  src/output/WaylandClipboardProcess.h
  src/output/WlClipboardDelivery.h
  src/output/YdotoolDelivery.h
  src/output/YdotoolSetup.h
  src/platform/AtSpiTargetProvider.h
  src/platform/KGlobalAccelShortcutBinder.h
  src/platform/LinuxDesktopIntegration.h
  src/platform/LinuxStyleChoice.h
  src/platform/MediaPauseController.h
  src/platform/PortalScreenshotContextProvider.h
  src/platform/PortalGlobalShortcutBinder.h
  src/platform/WaylandLayerShell.h
  src/platform/atspi/AtSpiAccess.h
  src/platform/atspi/AtSpiCorrectionObserver.h
  src/platform/atspi/AtSpiTargetSnapshot.h
)

set(SPEECHER_PLATFORM_MAC_SOURCES
  src/output/mac/MacPasteDelivery.mm
  src/platform/mac/MacCorrectionObserver.mm
  src/platform/mac/MacGlobalShortcutBinder.mm
  src/platform/mac/MacMediaController.mm
  src/platform/mac/MacPopupPositioner.mm
  src/platform/mac/MacScreenshotContextProvider.mm
  src/platform/mac/MacTargetProvider.mm
)

set(SPEECHER_PLATFORM_MAC_HEADERS
  src/output/mac/MacPasteDelivery.h
  src/platform/mac/MacCorrectionObserver.h
  src/platform/mac/MacGlobalShortcutBinder.h
  src/platform/mac/MacMediaController.h
  src/platform/mac/MacPopupPositioner.h
  src/platform/mac/MacScreenshotContextProvider.h
  src/platform/mac/MacTargetProvider.h
)

set(SPEECHER_PLATFORM_WIN_SOURCES
  src/output/win/WinPasteDelivery.cpp
  src/platform/win/WinCorrectionObserver.cpp
  src/platform/win/WinGlobalShortcutBinder.cpp
  src/platform/win/WinMediaController.cpp
  src/platform/win/WinScreenshotContextProvider.cpp
  src/platform/win/WinTargetProvider.cpp
)
set(SPEECHER_PLATFORM_WIN_HEADERS
  src/output/win/WinPasteDelivery.h
  src/platform/win/WinCorrectionObserver.h
  src/platform/win/WinGlobalShortcutBinder.h
  src/platform/win/WinMediaController.h
  src/platform/win/WinScreenshotContextProvider.h
  src/platform/win/WinTargetProvider.h
)

set(SPEECHER_APP_SOURCES
  src/app/ApplicationController.cpp
  src/app/CommandLine.cpp
  src/app/CompositionSockets.cpp
  src/app/PlatformSelection.cpp
  src/app/SingleInstanceIpc.cpp
)

set(SPEECHER_APP_HEADERS
  src/app/AppFrontEnd.h
  src/app/ApplicationController.h
  src/app/CommandLine.h
  src/app/CompositionSockets.h
  src/app/PlatformComposition.h
  src/app/SingleInstanceIpc.h
  src/app/UpdateController.h
)

set(SPEECHER_APP_LINUX_SOURCES
  src/app/LinuxComposition.cpp
)

set(SPEECHER_APP_LINUX_HEADERS
  src/app/LinuxComposition.h
)

set(SPEECHER_APP_MAC_SOURCES
  src/app/MacComposition.mm
  src/app/MacSparkleUpdater.mm
)

set(SPEECHER_APP_MAC_HEADERS
  src/app/MacComposition.h
  src/app/MacSparkleUpdater.h
)

set(SPEECHER_APP_WIN_SOURCES
  src/app/WindowsComposition.cpp
)

set(SPEECHER_APP_WIN_HEADERS
  src/app/WindowsComposition.h
)

set(SPEECHER_FRONTEND_QT_SOURCES
  src/frontend/qt/BindingRows.cpp
  src/frontend/qt/CollectionRow.cpp
  src/frontend/qt/OutputCustomRows.cpp
  src/frontend/qt/ProviderCustomRows.cpp
  src/frontend/qt/QtFrontEnd.cpp
  src/frontend/qt/QtPopupSurface.cpp
  src/frontend/qt/SchemaSettingsPage.cpp
  src/frontend/qt/WritingProfileGrid.cpp
  src/ui/AccessibilityNotice.cpp
  src/ui/AppWindow.cpp
  src/ui/DictationPage.cpp
  src/ui/SetupAssistant.cpp
  src/ui/setup/SetupPages.cpp
  src/ui/settings/SettingsPageSupport.cpp
  src/ui/settings/SettingsPageSet.cpp
  src/ui/HostStylePlugin.cpp
  src/ui/InlineMessage.cpp
  src/ui/Theme.cpp
  src/ui/TranscriberPopup.cpp
  src/ui/WaveformWidget.cpp
)

set(SPEECHER_FRONTEND_QT_HEADERS
  src/frontend/qt/BindingRows.h
  src/frontend/qt/CollectionRow.h
  src/frontend/qt/OutputCustomRows.h
  src/frontend/qt/ProviderCustomRows.h
  src/frontend/qt/QtFrontEnd.h
  src/frontend/qt/QtPopupSurface.h
  src/frontend/qt/SchemaSettingsPage.h
  src/frontend/qt/WritingProfileGrid.h
  src/ui/AccessibilityNotice.h
  src/ui/AppPage.h
  src/ui/AppWindow.h
  src/ui/DictationPage.h
  src/ui/SetupAssistant.h
  src/ui/setup/SetupPages.h
  src/ui/settings/SettingsPageSupport.h
  src/ui/settings/SettingsPageSet.h
  src/ui/HostStylePlugin.h
  src/ui/InlineMessage.h
  src/ui/Theme.h
  src/ui/TranscriberPopup.h
  src/ui/WaveformWidget.h
)

set(SPEECHER_FRONTEND_QT_LINUX_SOURCES
  src/output/YdotoolSetupFlow.cpp
  src/ui/setup/LinuxGlobalShortcutSetupPage.cpp
)

set(SPEECHER_FRONTEND_QT_LINUX_HEADERS
  src/output/YdotoolSetupFlow.h
  src/ui/setup/LinuxGlobalShortcutSetupPage.h
)

set(SPEECHER_FRONTEND_QT_MAC_SOURCES
  src/platform/mac/MacWindowChrome.mm
)

set(SPEECHER_FRONTEND_QT_MAC_HEADERS
  src/platform/mac/MacWindowChrome.h
)

set(SPEECHER_FRONTEND_MAC_SOURCES
  src/frontend/mac/MacCustomRows.cpp
  src/frontend/mac/MacFrontEnd.mm
  src/frontend/mac/SpeecherBridge.mm
)

set(SPEECHER_FRONTEND_MAC_HEADERS
  src/frontend/mac/MacCustomRows.h
  src/frontend/mac/MacFrontEnd.h
  src/frontend/mac/SpeecherBridge.h
)

set(SPEECHER_FRONTEND_MAC_SWIFT_SOURCES
  src/frontend/mac/swift/AppModel.swift
  src/frontend/mac/swift/Collections.swift
  src/frontend/mac/swift/DictationPanel.swift
  src/frontend/mac/swift/MenuBarExtra.swift
  src/frontend/mac/swift/Panes.swift
  src/frontend/mac/swift/Rows.swift
  src/frontend/mac/swift/SettingsWindow.swift
  src/frontend/mac/swift/ShortcutPane.swift
  src/frontend/mac/swift/SpeecherMacUI.swift
)

set(SPEECHER_FRONTEND_WIN_SOURCES
  src/frontend/win/WinFrontEnd.cpp
  src/frontend/win/WinUiHost.cpp
)

set(SPEECHER_FRONTEND_WIN_HEADERS
  src/frontend/win/WinFrontEnd.h
  src/frontend/win/WinUiHost.h
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
target_include_directories(speecher_domain PUBLIC src)
target_link_libraries(speecher_domain PUBLIC Qt6::Core)

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

add_library(speecher_platform STATIC
  ${SPEECHER_PLATFORM_SOURCES}
  ${SPEECHER_PLATFORM_HEADERS}
)
target_include_directories(speecher_platform PUBLIC src)
# Gui, not Widgets: the popup port speaks PopupSurface so that a front end
# built on something other than Qt Widgets can still be positioned.
target_link_libraries(speecher_platform PUBLIC speecher_workflow Qt6::Gui Qt6::Multimedia)
if(UNIX AND NOT APPLE)
  target_sources(speecher_platform PRIVATE
    ${SPEECHER_PLATFORM_LINUX_SOURCES}
    ${SPEECHER_PLATFORM_LINUX_HEADERS}
  )
  # Public: ClipboardDelivery's members and TextDelivery's method order differ
  # with the Wayland and ydotool backends compiled in.
  target_compile_definitions(speecher_platform PUBLIC SPEECHER_WITH_WAYLAND=1)
  target_link_libraries(speecher_platform PUBLIC Qt6::DBus)
endif()
if(APPLE)
  target_sources(speecher_platform PRIVATE
    ${SPEECHER_PLATFORM_MAC_SOURCES}
    ${SPEECHER_PLATFORM_MAC_HEADERS}
  )
  # Public: TextDelivery's method order and backend set differ with the macOS
  # keyboard-paste backend compiled in.
  target_compile_definitions(speecher_platform PUBLIC SPEECHER_WITH_MAC=1)
  target_link_libraries(speecher_platform PUBLIC
    "-framework AppKit"
    "-framework Carbon"
    "-framework ApplicationServices"
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
    uiautomationcore user32 ole32 shell32 gdi32 windowsapp
  )
endif()
if(ATSPI_FOUND)
  target_compile_definitions(speecher_platform PUBLIC SPEECHER_WITH_ATSPI=1)
  target_link_libraries(speecher_platform PUBLIC PkgConfig::ATSPI)
endif()

# The application: sessions, permissions and single-instance IPC, with no user
# interface of its own. Deliberately free of Qt Widgets so that a front end
# built on something else can drive it; see
# docs/adr/0001-per-platform-front-ends.md.
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

add_library(speecher_frontend_qt STATIC
  ${SPEECHER_FRONTEND_QT_SOURCES}
  ${SPEECHER_FRONTEND_QT_HEADERS}
)
target_include_directories(speecher_frontend_qt PUBLIC src)
target_link_libraries(speecher_frontend_qt PUBLIC speecher_app Qt6::Widgets)
if(UNIX AND NOT APPLE)
  target_sources(speecher_frontend_qt PRIVATE
    ${SPEECHER_FRONTEND_QT_LINUX_SOURCES}
    ${SPEECHER_FRONTEND_QT_LINUX_HEADERS}
  )
  # The setup assistant and the output page drive ydotool, which only exists on
  # the Linux side of the platform layer.
  target_compile_definitions(speecher_frontend_qt PUBLIC SPEECHER_WITH_YDOTOOL=1)
endif()
if(APPLE)
  target_sources(speecher_frontend_qt PRIVATE
    ${SPEECHER_FRONTEND_QT_MAC_SOURCES}
    ${SPEECHER_FRONTEND_QT_MAC_HEADERS}
  )
endif()
if(KF6WidgetsAddons_FOUND)
  target_compile_definitions(speecher_frontend_qt PUBLIC
    SPEECHER_WITH_KASSISTANT=1
    SPEECHER_WITH_KPAGEWIDGET=1
  )
  target_link_libraries(speecher_frontend_qt PUBLIC KF6::WidgetsAddons)
endif()
if(KF6GlobalAccel_FOUND)
  target_compile_definitions(speecher_platform PRIVATE SPEECHER_WITH_KGLOBALACCEL=1)
  target_link_libraries(speecher_platform PRIVATE KF6::GlobalAccel)
endif()
if(KF6WidgetsAddons_FOUND AND KF6ColorScheme_FOUND)
  target_compile_definitions(speecher_frontend_qt PRIVATE SPEECHER_WITH_KCOLORSCHEME=1)
  target_link_libraries(speecher_frontend_qt PRIVATE KF6::ColorScheme)
endif()

if(APPLE AND SPEECHER_WITH_SWIFT_UI)
  # swiftc writes the generated header itself, so the directory has to be there
  # before the first compile.
  set(SPEECHER_SWIFT_HEADER_DIR "${CMAKE_CURRENT_BINARY_DIR}/swift")
  file(MAKE_DIRECTORY "${SPEECHER_SWIFT_HEADER_DIR}")

  # Swift only: CMake cannot mix Swift with another language in one target.
  add_library(speecher_frontend_mac_swift STATIC
    ${SPEECHER_FRONTEND_MAC_SWIFT_SOURCES}
  )
  set_target_properties(speecher_frontend_mac_swift PROPERTIES
    Swift_MODULE_NAME SpeecherUI
    Swift_LANGUAGE_VERSION 5
    AUTOMOC OFF
    AUTORCC OFF
    AUTOUIC OFF
  )
  # SpeecherBridge.h is both the Objective-C facade and the bridging header, so
  # Swift sees the app through it and through nothing else. The header swiftc
  # emits in return is what lets Objective-C++ open the window.
  target_compile_options(speecher_frontend_mac_swift PRIVATE
    "SHELL:-import-objc-header ${CMAKE_CURRENT_SOURCE_DIR}/src/frontend/mac/SpeecherBridge.h"
    "SHELL:-emit-objc-header -emit-objc-header-path ${SPEECHER_SWIFT_HEADER_DIR}/SpeecherUI-Swift.h"
  )

  add_library(speecher_frontend_mac STATIC
    ${SPEECHER_FRONTEND_MAC_SOURCES}
    ${SPEECHER_FRONTEND_MAC_HEADERS}
  )
  target_include_directories(speecher_frontend_mac
    PUBLIC src
    PRIVATE "${SPEECHER_SWIFT_HEADER_DIR}"
  )
  set_target_properties(speecher_frontend_mac PROPERTIES
    AUTOMOC OFF
    # Otherwise the Swift dependency below makes CMake archive this one with
    # swiftc, which rejects the Objective-C++ flags.
    LINKER_LANGUAGE OBJCXX
  )
  # ARC in these two files only: they hold Objective-C objects in C++ members and
  # pass blocks across the wall, which manual retain/release would only obscure.
  target_compile_options(speecher_frontend_mac PRIVATE
    $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>
  )
  # Qt, still: macOS reuses the setup assistant from the Qt front end.
  target_link_libraries(speecher_frontend_mac PUBLIC
    speecher_frontend_qt speecher_frontend_mac_swift "-framework AppKit"
  )
  # Ninja cannot see that the .mm files need the generated header, so order the
  # whole Swift target ahead of them.
  add_dependencies(speecher_frontend_mac speecher_frontend_mac_swift)
endif()

if(WIN32 AND SPEECHER_WITH_WINUI)
  add_library(speecher_frontend_win STATIC
    ${SPEECHER_FRONTEND_WIN_SOURCES}
    ${SPEECHER_FRONTEND_WIN_HEADERS}
  )
  target_include_directories(speecher_frontend_win PUBLIC src)
  target_link_libraries(speecher_frontend_win PUBLIC speecher_app winappsdk)
endif()

qt_add_executable(speecher
  src/app/main.cpp
)

target_include_directories(speecher PRIVATE src)
target_compile_definitions(speecher PRIVATE
  SPEECHER_VERSION="${SPEECHER_VERSION}"
  SPEECHER_BUILD_NUMBER=${SPEECHER_BUILD_NUMBER}
)
if(TARGET speecher_frontend_win)
  target_compile_definitions(speecher PRIVATE SPEECHER_WITH_WINUI=1)
  target_link_libraries(speecher PRIVATE speecher_frontend_win Qt6::Widgets)
else()
  target_link_libraries(speecher PRIVATE speecher_frontend_qt)
endif()
if(TARGET speecher_frontend_mac)
  target_compile_definitions(speecher PRIVATE SPEECHER_WITH_SWIFT_UI=1)
  target_link_libraries(speecher PRIVATE speecher_frontend_mac)
  # Same reason as the front end itself: swiftc would otherwise be handed the
  # linker flags Qt's plugin importer contributes, and reject them. The Swift
  # runtime still arrives, autolinked from the Swift objects.
  set_target_properties(speecher PROPERTIES LINKER_LANGUAGE CXX)
endif()

if(WIN32)
  target_sources(speecher PRIVATE packaging/windows/speecher.manifest)
endif()
if(TARGET speecher_frontend_win)
  set_target_properties(speecher PROPERTIES WIN32_EXECUTABLE TRUE)
  add_custom_command(TARGET speecher POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
      "${SPEECHER_WASDK_BOOTSTRAP_DLL}" "$<TARGET_FILE_DIR:speecher>"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
      ${SPEECHER_WASDK_WINMDS} "$<TARGET_FILE_DIR:speecher>"
    COMMAND_EXPAND_LISTS
    COMMENT "Copying Windows App SDK runtime files"
    VERBATIM
  )
endif()

if(APPLE)
  find_program(SPEECHER_DITTO_EXECUTABLE ditto REQUIRED)
  set(SPEECHER_MACOS_RESOURCES
    packaging/macos/speecher.icns
    packaging/macos/speecher-menubar.png
    packaging/macos/speecher-menubar@2x.png
  )
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
  set_target_properties(speecher-ydotool-setup PROPERTIES
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
endif()

if(SPEECHER_WITH_QKEYCHAIN)
  if(SPEECHER_RELEASE_BUILD)
    find_package(Qt6Keychain REQUIRED)
  else()
    find_package(Qt6Keychain QUIET)
  endif()
  if(WIN32 AND NOT Qt6Keychain_FOUND)
    include(FetchContent)
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
message(STATUS "  KDE GlobalAccel/WidgetsAddons/ColorScheme: ${KF6GlobalAccel_FOUND}/${KF6WidgetsAddons_FOUND}/${KF6ColorScheme_FOUND}")
message(STATUS "  LayerShellQt: ${LayerShellQt_FOUND}")
message(STATUS "  Qt WebSockets: ${Qt6WebSockets_FOUND}")
message(STATUS "  QtKeychain: ${Qt6Keychain_FOUND}")
message(STATUS "  AT-SPI: ${ATSPI_FOUND}")
message(STATUS "  Wayland helper: ${WAYLAND_CLIENT_FOUND}/${WAYLAND_SCANNER_EXECUTABLE}")

include(GNUInstallDirs)
if(NOT DEFINED SPEECHER_DESKTOP_EXEC)
  set(SPEECHER_DESKTOP_EXEC "${CMAKE_INSTALL_FULL_BINDIR}/speecher")
endif()
set(SPEECHER_YDOTOOL_HELPER_PATH "${CMAKE_INSTALL_FULL_LIBEXECDIR}/speecher/speecher-ydotool-setup")
target_compile_definitions(speecher_platform PUBLIC
  SPEECHER_YDOTOOL_HELPER_PATH="${SPEECHER_YDOTOOL_HELPER_PATH}"
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
if(TARGET speecher-wayland-clipboard)
  install(TARGETS speecher-wayland-clipboard
    RUNTIME DESTINATION ${CMAKE_INSTALL_LIBEXECDIR}/speecher)
endif()
# The .desktop entry, hicolor icon and post-install hooks that refresh the
# desktop caches are all freedesktop-only.
if(UNIX AND NOT APPLE)
  install(FILES "${CMAKE_CURRENT_BINARY_DIR}/io.github.firemonster612.speecher.desktop" DESTINATION ${SPEECHER_INSTALL_APPLICATIONS_DIR})
  install(FILES packaging/io.github.firemonster612.speecher.svg DESTINATION ${SPEECHER_INSTALL_HICOLOR_DIR}/scalable/apps)
  install(SCRIPT "${CMAKE_CURRENT_BINARY_DIR}/post-install.cmake")
endif()

if(SPEECHER_BUILD_TESTS)
  enable_testing()
  qt_add_executable(speecher_tests
    tests/test_main.cpp
    tests/test_ui.cpp
    tests/test_app_window.cpp
    tests/test_transcript_state.cpp
    tests/test_bindings.cpp
    tests/test_settings.cpp
    tests/test_settings_schema.cpp
    tests/test_provider_registry.cpp
    tests/test_platform_composition.cpp
    tests/test_platform_live.cpp
    tests/test_single_instance_ipc.cpp
    tests/test_delivery.cpp
    tests/test_dictation_session_lifecycle.cpp
    tests/test_dictation_session_refinement.cpp
    tests/test_refiners.cpp
    tests/test_vocabulary.cpp
    tests/test_provider_auth.cpp
    tests/test_claude_voice.cpp
    tests/test_codex_dictation.cpp
    tests/test_audio_pcm_converter.cpp
  )
  if(UNIX)
    target_sources(speecher_tests PRIVATE tests/test_update_controller.cpp)
  endif()
  if(WIN32)
    target_sources(speecher_tests PRIVATE tests/test_win_platform.cpp)
  endif()
  target_include_directories(speecher_tests PRIVATE src)
  target_compile_definitions(speecher_tests PRIVATE
    SPEECHER_VERSION="${SPEECHER_VERSION}"
    SPEECHER_BUILD_NUMBER=${SPEECHER_BUILD_NUMBER}
  )
  target_link_libraries(speecher_tests PRIVATE
    speecher_frontend_qt Qt6::Test
  )
  if(UNIX AND NOT APPLE)
    target_sources(speecher_tests PRIVATE tests/test_linux_style_choice.cpp)
  endif()
  if(TARGET speecher_frontend_mac)
    target_sources(speecher_tests PRIVATE tests/test_mac_frontend.mm)
    target_compile_definitions(speecher_tests PRIVATE SPEECHER_WITH_SWIFT_UI=1)
    # The generated Swift header is private to the front-end library; the test
    # exercises the Swift panel through it, so it needs the same include.
    target_include_directories(speecher_tests PRIVATE "${SPEECHER_SWIFT_HEADER_DIR}")
    # ARC, as the bridge itself is built: an unmanaged bridge in one test would
    # outlive its controller and every suite after it.
    target_compile_options(speecher_tests PRIVATE
      $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>
    )
    target_link_libraries(speecher_tests PRIVATE speecher_frontend_mac)
    set_target_properties(speecher_tests PROPERTIES LINKER_LANGUAGE CXX)
  endif()
  add_test(NAME speecher_tests COMMAND speecher_tests)
  set_tests_properties(speecher_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

  qt_add_executable(speecher_atspi_fixture
    tests/atspi_fixture.cpp
  )
  target_link_libraries(speecher_atspi_fixture PRIVATE Qt6::Widgets)
endif()
