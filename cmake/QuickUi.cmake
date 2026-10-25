# The Qt Quick UI (src/frontend/quick), compiled into the speecher executable:
# one static QML module per directory, its QML and JavaScript ahead-of-time
# compiled and kept at their paths under qrc:/qt/qml, and the fonts as
# resources. Each directory's qmldir names the singletons and the JavaScript
# modules.

qt_policy(SET QTP0001 NEW)
# A subdirectory's qmldir sends its imports to the module, so every QML file,
# a page's parts in pages/home/ included, is one of the module's types.
qt_policy(SET QTP0004 NEW)

set(QT_QML_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/qml")
set(SPEECHER_QUICK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/frontend/quick")

# Adds the static QML module `uri` as `target` (and `target`plugin) from the
# .qml and .js files under dir. SOURCES are the module's C++, and LIBRARIES
# what it and its users link.
function(speecher_add_qml_module target uri dir)
  cmake_parse_arguments(PARSE_ARGV 3 arg "" "" "SOURCES;LIBRARIES")
  file(GLOB_RECURSE files CONFIGURE_DEPENDS RELATIVE "${dir}" "${dir}/*.qml" "${dir}/*.js")
  set(paths "")
  foreach(file IN LISTS files)
    set_source_files_properties("${dir}/${file}" PROPERTIES QT_RESOURCE_ALIAS "${file}")
    # JavaScript is a module only where qmldir names it; the rest is imported by path.
    if(file MATCHES "\\.js$")
      set_source_files_properties("${dir}/${file}" PROPERTIES QT_QML_SKIP_QMLDIR_ENTRY TRUE)
    endif()
    list(APPEND paths "${dir}/${file}")
  endforeach()
  # "Name 1.0 path" and "singleton Name 1.0 path" lines.
  file(STRINGS "${dir}/qmldir" entries REGEX "^(singleton )?[A-Z][A-Za-z]* [0-9.]+ ")
  foreach(entry IN LISTS entries)
    string(REGEX MATCH "^(singleton )?([A-Za-z]+) [0-9.]+ (.+)$" matched "${entry}")
    set(file "${CMAKE_MATCH_3}")
    if(NOT file IN_LIST files)
      continue()
    endif()
    set_source_files_properties("${dir}/${file}" PROPERTIES
      QT_QML_SKIP_QMLDIR_ENTRY FALSE
      QT_QML_SOURCE_TYPENAME "${CMAKE_MATCH_2}")
    if(CMAKE_MATCH_1)
      set_source_files_properties("${dir}/${file}" PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
    endif()
  endforeach()
  qt_add_library(${target} STATIC)
  qt_add_qml_module(${target} URI ${uri} VERSION 1.0 QML_FILES ${paths} SOURCES ${arg_SOURCES})
  target_link_libraries(${target} PUBLIC ${arg_LIBRARIES})
endfunction()

speecher_add_qml_module(speecher_quick_theme Speecher.Theme "${SPEECHER_QUICK_DIR}/qml/Speecher/Theme")
speecher_add_qml_module(speecher_quick_demo Speecher.Demo "${SPEECHER_QUICK_DIR}/qml/Speecher/Demo")
speecher_add_qml_module(speecher_quick_wording Speecher.Wording "${SPEECHER_QUICK_DIR}/qml/Speecher/Wording")
speecher_add_qml_module(speecher_quick_controls Speecher.Controls "${SPEECHER_QUICK_DIR}/qml/Speecher/Controls")
speecher_add_qml_module(speecher_quick_app Speecher.App "${SPEECHER_QUICK_DIR}/qml/Speecher/App")
speecher_add_qml_module(speecher_quick_popup Speecher.Popup "${SPEECHER_QUICK_DIR}/qml/Speecher/Popup")
# Every area is C++ over core, and Library.qml words LibraryLists.
speecher_add_qml_module(speecher_quick_backend Speecher.Backend "${SPEECHER_QUICK_DIR}/qml/Speecher/Backend"
  SOURCES
    "${SPEECHER_QUICK_DIR}/host/QuickAccounts.cpp"
    "${SPEECHER_QUICK_DIR}/host/QuickAccounts.h"
    "${SPEECHER_QUICK_DIR}/host/QuickAppearance.cpp"
    "${SPEECHER_QUICK_DIR}/host/QuickAppearance.h"
    "${SPEECHER_QUICK_DIR}/host/QuickAudio.cpp"
    "${SPEECHER_QUICK_DIR}/host/QuickAudio.h"
    "${SPEECHER_QUICK_DIR}/host/QuickDictation.cpp"
    "${SPEECHER_QUICK_DIR}/host/QuickDictation.h"
    "${SPEECHER_QUICK_DIR}/host/QuickModels.cpp"
    "${SPEECHER_QUICK_DIR}/host/QuickModels.h"
    "${SPEECHER_QUICK_DIR}/host/QuickInsights.cpp"
    "${SPEECHER_QUICK_DIR}/host/QuickInsights.h"
    "${SPEECHER_QUICK_DIR}/host/QuickLibrary.cpp"
    "${SPEECHER_QUICK_DIR}/host/QuickLibrary.h"
    "${SPEECHER_QUICK_DIR}/host/QuickSettings.cpp"
    "${SPEECHER_QUICK_DIR}/host/QuickSettings.h"
    "${SPEECHER_QUICK_DIR}/host/QuickStatus.cpp"
    "${SPEECHER_QUICK_DIR}/host/QuickStatus.h"
    "${SPEECHER_QUICK_DIR}/host/QuickUpdates.cpp"
    "${SPEECHER_QUICK_DIR}/host/QuickUpdates.h"
    "${SPEECHER_QUICK_DIR}/host/QuickTranscription.cpp"
    "${SPEECHER_QUICK_DIR}/host/QuickTranscription.h"
    "${SPEECHER_QUICK_DIR}/host/QuickSystem.cpp"
    "${SPEECHER_QUICK_DIR}/host/QuickSystem.h"
  LIBRARIES speecher_app Qt6::Qml
)
# The generated type registration includes those headers by file name.
target_include_directories(speecher_quick_backend PRIVATE "${SPEECHER_QUICK_DIR}/host")

target_sources(speecher PRIVATE
  "${SPEECHER_QUICK_DIR}/host/QuickFrontEnd.cpp"
  "${SPEECHER_QUICK_DIR}/host/QuickFrontEnd.h"
  "${SPEECHER_QUICK_DIR}/host/QuickPopupWindow.cpp"
  "${SPEECHER_QUICK_DIR}/host/QuickPopupWindow.h"
)
# QuickFrontEnd.cpp imports these plugins itself. Qt's own QML modules load
# from the shared Qt at run time, so the import scan, which links them for a
# static Qt, is off.
set_target_properties(speecher PROPERTIES QT_QML_MODULE_NO_IMPORT_SCAN TRUE)
# The UI's QML module plugins, linked statically into whatever loads the UI.
set(SPEECHER_QUICK_PLUGINS
  speecher_quick_themeplugin
  speecher_quick_demoplugin
  speecher_quick_wordingplugin
  speecher_quick_controlsplugin
  speecher_quick_backendplugin
  speecher_quick_appplugin
  speecher_quick_popupplugin
)
target_link_libraries(speecher PRIVATE Qt6::Quick ${SPEECHER_QUICK_PLUGINS})
file(GLOB SPEECHER_QUICK_FONTS "${SPEECHER_QUICK_DIR}/assets/*/*.ttf")
qt_add_resources(speecher speecher_quick_fonts
  PREFIX /speecher
  BASE "${SPEECHER_QUICK_DIR}"
  FILES ${SPEECHER_QUICK_FONTS}
)
