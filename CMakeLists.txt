cmake_minimum_required(VERSION 3.24)

project(speecher VERSION 0.1.0 LANGUAGES CXX)

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
find_package(Qt6 6.5 QUIET COMPONENTS WebSockets)
if(SPEECHER_WITH_LAYER_SHELL)
  find_package(LayerShellQt QUIET)
endif()
if(SPEECHER_BUILD_TESTS)
  find_package(Qt6 6.5 REQUIRED COMPONENTS Test)
endif()

set(SPEECHER_DOMAIN_SOURCES
  src/core/OutputMethod.cpp
  src/core/SettingsStore.cpp
  src/core/SecretStore.cpp
  src/core/TranscriptState.cpp
  src/core/VocabularyLimit.cpp
  src/core/WordPreview.cpp
  src/dictation/DictationTypes.cpp
  src/providers/ProviderRegistry.cpp
)

set(SPEECHER_DOMAIN_HEADERS
  src/core/AppSettings.h
  src/core/OutputMethod.h
  src/core/SettingsStore.h
  src/core/SecretStore.h
  src/core/TranscriptState.h
  src/core/VocabularyLimit.h
  src/core/WordPreview.h
  src/dictation/DictationInterfaces.h
  src/dictation/DictationTypes.h
  src/providers/ProviderRegistry.h
)

set(SPEECHER_WORKFLOW_SOURCES
  src/dictation/DictationSession.cpp
)

set(SPEECHER_WORKFLOW_HEADERS
  src/dictation/DictationSession.h
)

set(SPEECHER_PROVIDER_SOURCES
  src/providers/ClaudeCredentials.cpp
  src/providers/ClaudeSpeechTranscriber.cpp
  src/providers/ClaudeVoiceClient.cpp
  src/providers/OpenAiAuthProvider.cpp
  src/providers/OpenAiRefiner.cpp
  src/providers/OpenAiTranscriptRefiner.cpp
)

set(SPEECHER_PROVIDER_HEADERS
  src/providers/ClaudeCredentials.h
  src/providers/ClaudeSpeechTranscriber.h
  src/providers/ClaudeVoiceClient.h
  src/providers/OpenAiAuthProvider.h
  src/providers/OpenAiRefiner.h
  src/providers/OpenAiTranscriptRefiner.h
)

set(SPEECHER_PLATFORM_SOURCES
  src/core/AudioCapture.cpp
  src/core/MediaPauseController.cpp
  src/output/ClipboardDelivery.cpp
  src/output/QtClipboardDelivery.cpp
  src/output/TextDelivery.cpp
  src/output/WlClipboardDelivery.cpp
  src/output/YdotoolDelivery.cpp
  src/output/YdotoolSetup.cpp
  src/platform/PlatformIntegration.cpp
  src/ui/WaylandLayerShell.cpp
)

set(SPEECHER_PLATFORM_HEADERS
  src/core/AudioCapture.h
  src/core/MediaPauseController.h
  src/output/ClipboardDelivery.h
  src/output/DeliveryResult.h
  src/output/QtClipboardDelivery.h
  src/output/TextDelivery.h
  src/output/WlClipboardDelivery.h
  src/output/YdotoolDelivery.h
  src/output/YdotoolSetup.h
  src/platform/PlatformIntegration.h
  src/ui/WaylandLayerShell.h
)

set(SPEECHER_APP_UI_SOURCES
  src/app/ApplicationController.cpp
  src/app/SingleInstanceIpc.cpp
  src/ui/MainWindow.cpp
  src/ui/SettingsDialog.cpp
  src/ui/Theme.cpp
  src/ui/TranscriberPopup.cpp
  src/ui/WaveformWidget.cpp
)

set(SPEECHER_APP_UI_HEADERS
  src/app/ApplicationController.h
  src/app/SingleInstanceIpc.h
  src/ui/MainWindow.h
  src/ui/SettingsDialog.h
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
target_link_libraries(speecher_providers PUBLIC speecher_domain Qt6::Network)

add_library(speecher_platform STATIC
  ${SPEECHER_PLATFORM_SOURCES}
  ${SPEECHER_PLATFORM_HEADERS}
)
target_include_directories(speecher_platform PUBLIC src)
target_link_libraries(speecher_platform PUBLIC speecher_domain Qt6::Widgets Qt6::Multimedia Qt6::DBus)

add_library(speecher_app_ui STATIC
  ${SPEECHER_APP_UI_SOURCES}
  ${SPEECHER_APP_UI_HEADERS}
)
target_include_directories(speecher_app_ui PUBLIC src)
target_compile_definitions(speecher_app_ui PUBLIC SPEECHER_VERSION="${PROJECT_VERSION}")
target_link_libraries(speecher_app_ui PUBLIC
  speecher_domain speecher_workflow speecher_providers speecher_platform Qt6::Widgets
)

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
if(Qt6WebSockets_FOUND)
  target_compile_definitions(speecher_providers PUBLIC SPEECHER_WITH_QT_WEBSOCKETS=1)
  target_link_libraries(speecher_providers PUBLIC Qt6::WebSockets)
endif()
if(LayerShellQt_FOUND)
  target_compile_definitions(speecher_platform PUBLIC SPEECHER_WITH_LAYER_SHELL=1)
  target_link_libraries(speecher_platform PUBLIC LayerShellQt::Interface)
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
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/local.speecher.desktop" DESTINATION ${SPEECHER_INSTALL_APPLICATIONS_DIR})
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/local.speecher.setup-ydotool.policy" DESTINATION ${CMAKE_INSTALL_DATADIR}/polkit-1/actions)
install(FILES packaging/local.speecher.svg DESTINATION ${SPEECHER_INSTALL_HICOLOR_DIR}/scalable/apps)
install(SCRIPT "${CMAKE_CURRENT_BINARY_DIR}/post-install.cmake")

if(SPEECHER_BUILD_TESTS)
  enable_testing()
  qt_add_executable(speecher_tests
    tests/test_core.cpp
  )
  target_include_directories(speecher_tests PRIVATE src)
  target_compile_definitions(speecher_tests PRIVATE SPEECHER_VERSION="${PROJECT_VERSION}")
  target_link_libraries(speecher_tests PRIVATE
    speecher_domain speecher_workflow speecher_providers speecher_platform Qt6::Test
  )
  add_test(NAME speecher_tests COMMAND speecher_tests)
  set_tests_properties(speecher_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()
