cmake_minimum_required(VERSION 3.24)

project(speecher VERSION 0.1.0 LANGUAGES C CXX)

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

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Widgets Network Multimedia DBus)
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
  pkg_check_modules(ATSPI QUIET IMPORTED_TARGET atspi-2 gobject-2.0)
  pkg_check_modules(WAYLAND_CLIENT QUIET IMPORTED_TARGET wayland-client)
  pkg_get_variable(WAYLAND_PROTOCOLS_DATA_DIR wayland-protocols pkgdatadir)
endif()
find_package(Qt6 6.5 QUIET COMPONENTS WebSockets)
if(SPEECHER_WITH_LAYER_SHELL)
  find_package(LayerShellQt QUIET)
endif()
if(UNIX AND NOT APPLE)
  find_package(KF6WidgetsAddons QUIET)
  find_package(KF6ColorScheme QUIET)
  find_package(KF6GlobalAccel QUIET)
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
  src/providers/OpenAiAuthProvider.h
  src/providers/OpenAiRefiner.h
  src/providers/OpenAiTranscriptRefiner.h
  src/providers/TranscriptRefinementPrompt.h
)

set(SPEECHER_PLATFORM_SOURCES
  src/core/MediaPauseController.cpp
  src/output/ClipboardDelivery.cpp
  src/output/QtClipboardDelivery.cpp
  src/output/TextDelivery.cpp
  src/output/WaylandClipboardOwner.cpp
  src/output/WaylandClipboardProcess.cpp
  src/output/WlClipboardDelivery.cpp
  src/output/YdotoolDelivery.cpp
  src/output/YdotoolSetup.cpp
  src/platform/AtSpiTargetProvider.cpp
  src/platform/atspi/AtSpiAccess.cpp
  src/platform/atspi/AtSpiCorrectionObserver.cpp
  src/platform/atspi/AtSpiTargetSnapshot.cpp
  src/platform/PortalScreenshotContextProvider.cpp
  src/platform/audio/AudioPcmConverter.cpp
  src/platform/audio/QtAudioInput.cpp
  src/platform/WaylandLayerShell.cpp
)

set(SPEECHER_PLATFORM_HEADERS
  src/core/MediaPauseController.h
  src/output/ClipboardDelivery.h
  src/output/QtClipboardDelivery.h
  src/output/TextDelivery.h
  src/output/WaylandClipboardOwner.h
  src/output/WaylandClipboardProcess.h
  src/output/WlClipboardDelivery.h
  src/output/YdotoolDelivery.h
  src/output/YdotoolSetup.h
  src/platform/AtSpiTargetProvider.h
  src/platform/atspi/AtSpiAccess.h
  src/platform/atspi/AtSpiCorrectionObserver.h
  src/platform/atspi/AtSpiTargetSnapshot.h
  src/platform/PortalScreenshotContextProvider.h
  src/platform/audio/AudioPcmConverter.h
  src/platform/audio/QtAudioInput.h
  src/platform/WaylandLayerShell.h
)

set(SPEECHER_APP_UI_SOURCES
  src/app/ApplicationController.cpp
  src/app/LinuxComposition.cpp
  src/app/SingleInstanceIpc.cpp
  src/output/YdotoolSetupFlow.cpp
  src/ui/AccessibilityNotice.cpp
  src/ui/AppWindow.cpp
  src/ui/DictationPage.cpp
  src/ui/SetupAssistant.cpp
  src/ui/setup/SetupPages.cpp
  src/ui/settings/ApplicationSettingsPage.cpp
  src/ui/settings/AudioSettingsPage.cpp
  src/ui/settings/BindingsSettingsPage.cpp
  src/ui/settings/CorrectionsSettingsPage.cpp
  src/ui/settings/GeneralSettingsPage.cpp
  src/ui/settings/OutputSettingsPage.cpp
  src/ui/settings/ProviderSettingsPage.cpp
  src/ui/settings/RefinementSettingsPage.cpp
  src/ui/settings/SettingsPageSupport.cpp
  src/ui/settings/SettingsPageSet.cpp
  src/ui/settings/VocabularySettingsPage.cpp
  src/ui/Theme.cpp
  src/ui/TranscriberPopup.cpp
  src/ui/WaveformWidget.cpp
)

set(SPEECHER_APP_UI_HEADERS
  src/app/ApplicationController.h
  src/app/LinuxComposition.h
  src/app/SingleInstanceIpc.h
  src/output/YdotoolSetupFlow.h
  src/ui/AccessibilityNotice.h
  src/ui/AppPage.h
  src/ui/AppWindow.h
  src/ui/DictationPage.h
  src/ui/PopupPositioner.h
  src/ui/SetupAssistant.h
  src/ui/setup/SetupPages.h
  src/ui/settings/ApplicationSettingsPage.h
  src/ui/settings/AudioSettingsPage.h
  src/ui/settings/BindingsSettingsPage.h
  src/ui/settings/CorrectionsSettingsPage.h
  src/ui/settings/GeneralSettingsPage.h
  src/ui/settings/OutputSettingsPage.h
  src/ui/settings/ProviderSettingsPage.h
  src/ui/settings/RefinementSettingsPage.h
  src/ui/settings/SettingsPageSupport.h
  src/ui/settings/SettingsPageSet.h
  src/ui/settings/VocabularySettingsPage.h
  src/ui/Theme.h
  src/ui/TranscriberPopup.h
  src/ui/WaveformWidget.h
)

add_library(speecher_domain STATIC
  ${SPEECHER_DOMAIN_SOURCES}
  ${SPEECHER_DOMAIN_HEADERS}
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
target_link_libraries(speecher_platform PUBLIC speecher_workflow Qt6::Widgets Qt6::Multimedia Qt6::DBus)
if(ATSPI_FOUND)
  target_compile_definitions(speecher_platform PUBLIC SPEECHER_WITH_ATSPI=1)
  target_link_libraries(speecher_platform PUBLIC PkgConfig::ATSPI)
endif()

add_library(speecher_app_ui STATIC
  ${SPEECHER_APP_UI_SOURCES}
  ${SPEECHER_APP_UI_HEADERS}
)
target_include_directories(speecher_app_ui PUBLIC src)
target_compile_definitions(speecher_app_ui PUBLIC SPEECHER_VERSION="${PROJECT_VERSION}")
target_link_libraries(speecher_app_ui PUBLIC
  speecher_domain speecher_workflow speecher_providers speecher_platform Qt6::Widgets
)
if(KF6WidgetsAddons_FOUND)
  target_compile_definitions(speecher_app_ui PUBLIC
    SPEECHER_WITH_KASSISTANT=1
    SPEECHER_WITH_KPAGEWIDGET=1
  )
  target_link_libraries(speecher_app_ui PUBLIC KF6::WidgetsAddons)
endif()
if(KF6GlobalAccel_FOUND)
  target_compile_definitions(speecher_app_ui PRIVATE SPEECHER_WITH_KGLOBALACCEL=1)
  target_link_libraries(speecher_app_ui PRIVATE KF6::GlobalAccel)
endif()
if(KF6WidgetsAddons_FOUND AND KF6ColorScheme_FOUND)
  target_compile_definitions(speecher_app_ui PRIVATE SPEECHER_WITH_KCOLORSCHEME=1)
  target_link_libraries(speecher_app_ui PRIVATE KF6::ColorScheme)
endif()

qt_add_executable(speecher
  src/app/main.cpp
)

target_include_directories(speecher PRIVATE src)
target_compile_definitions(speecher PRIVATE
  SPEECHER_VERSION="${PROJECT_VERSION}"
)
target_link_libraries(speecher PRIVATE speecher_app_ui)

qt_add_executable(speecher-ydotool-setup
  src/setup/YdotoolSetupHelper.cpp
)
target_link_libraries(speecher-ydotool-setup PRIVATE Qt6::Core)

find_program(WAYLAND_SCANNER_EXECUTABLE wayland-scanner)
set(SPEECHER_EXT_DATA_CONTROL_XML
  "${WAYLAND_PROTOCOLS_DATA_DIR}/staging/ext-data-control/ext-data-control-v1.xml"
)
if(WAYLAND_CLIENT_FOUND AND WAYLAND_SCANNER_EXECUTABLE
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
  find_package(Qt6Keychain QUIET)
  if(Qt6Keychain_FOUND)
    target_compile_definitions(speecher_domain PUBLIC SPEECHER_WITH_QKEYCHAIN=1)
    target_link_libraries(speecher_domain PUBLIC qt6keychain)
  endif()
endif()

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
  packaging/local.speecher.desktop
  "${CMAKE_CURRENT_BINARY_DIR}/local.speecher.desktop"
  @ONLY
)
configure_file(
  packaging/local.speecher.setup-ydotool.policy.in
  "${CMAKE_CURRENT_BINARY_DIR}/local.speecher.setup-ydotool.policy"
  @ONLY
)
set(SPEECHER_INSTALL_APPLICATIONS_DIR "${CMAKE_INSTALL_DATADIR}/applications")
set(SPEECHER_INSTALL_HICOLOR_DIR "${CMAKE_INSTALL_DATADIR}/icons/hicolor")
configure_file(
  packaging/post-install.cmake.in
  "${CMAKE_CURRENT_BINARY_DIR}/post-install.cmake"
  @ONLY
)
install(TARGETS speecher RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(TARGETS speecher-ydotool-setup RUNTIME DESTINATION ${CMAKE_INSTALL_LIBEXECDIR}/speecher)
if(TARGET speecher-wayland-clipboard)
  install(TARGETS speecher-wayland-clipboard
    RUNTIME DESTINATION ${CMAKE_INSTALL_LIBEXECDIR}/speecher)
endif()
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/local.speecher.desktop" DESTINATION ${SPEECHER_INSTALL_APPLICATIONS_DIR})
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/local.speecher.setup-ydotool.policy" DESTINATION ${CMAKE_INSTALL_DATADIR}/polkit-1/actions)
install(FILES packaging/local.speecher.svg DESTINATION ${SPEECHER_INSTALL_HICOLOR_DIR}/scalable/apps)
install(SCRIPT "${CMAKE_CURRENT_BINARY_DIR}/post-install.cmake")

if(SPEECHER_BUILD_TESTS)
  enable_testing()
  qt_add_executable(speecher_tests
    tests/test_main.cpp
    tests/test_ui.cpp
    tests/test_app_window.cpp
    tests/test_transcript_state.cpp
    tests/test_bindings.cpp
    tests/test_settings.cpp
    tests/test_provider_registry.cpp
    tests/test_platform_live.cpp
    tests/test_single_instance_ipc.cpp
    tests/test_delivery.cpp
    tests/test_dictation_session_lifecycle.cpp
    tests/test_dictation_session_refinement.cpp
    tests/test_refiners.cpp
    tests/test_vocabulary.cpp
    tests/test_provider_auth.cpp
    tests/test_claude_voice.cpp
    tests/test_audio_pcm_converter.cpp
  )
  target_include_directories(speecher_tests PRIVATE src)
  target_compile_definitions(speecher_tests PRIVATE SPEECHER_VERSION="${PROJECT_VERSION}")
  target_link_libraries(speecher_tests PRIVATE
    speecher_app_ui Qt6::Test
  )
  add_test(NAME speecher_tests COMMAND speecher_tests)
  set_tests_properties(speecher_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

  qt_add_executable(speecher_atspi_fixture
    tests/atspi_fixture.cpp
  )
  target_link_libraries(speecher_atspi_fixture PRIVATE Qt6::Widgets)
endif()
