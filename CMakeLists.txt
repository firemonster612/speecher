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

set(SPEECHER_SOURCES
  src/app/ApplicationController.cpp
  src/app/SingleInstanceIpc.cpp
  src/core/AudioCapture.cpp
  src/core/MediaPauseController.cpp
  src/core/SettingsStore.cpp
  src/core/SecretStore.cpp
  src/core/TranscriptState.cpp
  src/core/VocabularyLimit.cpp
  src/core/WordPreview.cpp
  src/output/ClipboardDelivery.cpp
  src/output/TextDelivery.cpp
  src/output/WtypeDelivery.cpp
  src/providers/ClaudeCredentials.cpp
  src/providers/ClaudeVoiceClient.cpp
  src/providers/OpenAiAuthProvider.cpp
  src/providers/OpenAiRefiner.cpp
  src/ui/MainWindow.cpp
  src/ui/SettingsDialog.cpp
  src/ui/Theme.cpp
  src/ui/TranscriberPopup.cpp
  src/ui/WaveformWidget.cpp
  src/ui/WaylandLayerShell.cpp
)

set(SPEECHER_HEADERS
  src/app/ApplicationController.h
  src/app/SingleInstanceIpc.h
  src/core/AudioCapture.h
  src/core/MediaPauseController.h
  src/core/SettingsStore.h
  src/core/SecretStore.h
  src/core/TranscriptState.h
  src/core/VocabularyLimit.h
  src/core/WordPreview.h
  src/output/ClipboardDelivery.h
  src/output/TextDelivery.h
  src/output/WtypeDelivery.h
  src/providers/ClaudeCredentials.h
  src/providers/ClaudeVoiceClient.h
  src/providers/OpenAiAuthProvider.h
  src/providers/OpenAiRefiner.h
  src/ui/MainWindow.h
  src/ui/SettingsDialog.h
  src/ui/Theme.h
  src/ui/TranscriberPopup.h
  src/ui/WaveformWidget.h
  src/ui/WaylandLayerShell.h
)

qt_add_executable(speecher
  src/app/main.cpp
  ${SPEECHER_SOURCES}
  ${SPEECHER_HEADERS}
)

target_include_directories(speecher PRIVATE src)
target_compile_definitions(speecher PRIVATE
  SPEECHER_VERSION="${PROJECT_VERSION}"
)
target_link_libraries(speecher PRIVATE
  Qt6::Core Qt6::Widgets Qt6::Network Qt6::Multimedia Qt6::DBus
)
if(Qt6WebSockets_FOUND)
  target_compile_definitions(speecher PRIVATE SPEECHER_WITH_QT_WEBSOCKETS=1)
  target_link_libraries(speecher PRIVATE Qt6::WebSockets)
endif()
if(LayerShellQt_FOUND)
  target_compile_definitions(speecher PRIVATE SPEECHER_WITH_LAYER_SHELL=1)
  target_link_libraries(speecher PRIVATE LayerShellQt::Interface)
endif()

if(SPEECHER_WITH_QKEYCHAIN)
  find_package(Qt6Keychain QUIET)
  if(Qt6Keychain_FOUND)
    target_compile_definitions(speecher PRIVATE SPEECHER_WITH_QKEYCHAIN=1)
    target_link_libraries(speecher PRIVATE qt6keychain)
  endif()
endif()

include(GNUInstallDirs)
if(NOT DEFINED SPEECHER_DESKTOP_EXEC)
  set(SPEECHER_DESKTOP_EXEC "${CMAKE_INSTALL_FULL_BINDIR}/speecher")
endif()
configure_file(
  packaging/local.speecher.desktop
  "${CMAKE_CURRENT_BINARY_DIR}/local.speecher.desktop"
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
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/local.speecher.desktop" DESTINATION ${SPEECHER_INSTALL_APPLICATIONS_DIR})
install(FILES packaging/local.speecher.svg DESTINATION ${SPEECHER_INSTALL_HICOLOR_DIR}/scalable/apps)
install(SCRIPT "${CMAKE_CURRENT_BINARY_DIR}/post-install.cmake")

if(SPEECHER_BUILD_TESTS)
  enable_testing()
  qt_add_executable(speecher_tests
    tests/test_core.cpp
    src/core/SettingsStore.cpp
    src/core/SecretStore.cpp
    src/core/TranscriptState.cpp
    src/core/VocabularyLimit.cpp
    src/core/WordPreview.cpp
    src/output/ClipboardDelivery.cpp
    src/output/TextDelivery.cpp
    src/output/WtypeDelivery.cpp
    src/providers/ClaudeCredentials.cpp
    src/providers/ClaudeVoiceClient.cpp
    src/providers/OpenAiAuthProvider.cpp
    src/providers/OpenAiRefiner.cpp
  )
  target_include_directories(speecher_tests PRIVATE src)
  target_compile_definitions(speecher_tests PRIVATE SPEECHER_VERSION="${PROJECT_VERSION}")
  target_link_libraries(speecher_tests PRIVATE Qt6::Core Qt6::Widgets Qt6::Network Qt6::Test)
  add_test(NAME speecher_tests COMMAND speecher_tests)
endif()
