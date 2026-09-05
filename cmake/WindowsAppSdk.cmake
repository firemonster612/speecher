set(SPEECHER_WASDK_NUPKG_DIR "${CMAKE_BINARY_DIR}/nupkg")
file(MAKE_DIRECTORY "${SPEECHER_WASDK_NUPKG_DIR}")

function(speecher_fetch_wasdk_package out_var name version sha256)
  set(package_dir "${SPEECHER_WASDK_NUPKG_DIR}/${name}")
  set(archive "${SPEECHER_WASDK_NUPKG_DIR}/${name}.${version}.nupkg")
  if(NOT EXISTS "${package_dir}/.done")
    file(DOWNLOAD
      "https://api.nuget.org/v3-flatcontainer/${name}/${version}/${name}.${version}.nupkg"
      "${archive}"
      EXPECTED_HASH "SHA256=${sha256}"
      SHOW_PROGRESS
    )
    file(ARCHIVE_EXTRACT INPUT "${archive}" DESTINATION "${package_dir}")
    file(TOUCH "${package_dir}/.done")
  endif()
  set(${out_var} "${package_dir}" PARENT_SCOPE)
endfunction()

speecher_fetch_wasdk_package(SPEECHER_WASDK_FOUNDATION
  microsoft.windowsappsdk.foundation 2.3.9
  230bc605a3fc9ed689b2117056c5274923bf58b453fa44edde18a168bbf628be)
speecher_fetch_wasdk_package(SPEECHER_WASDK_INTERACTIVE
  microsoft.windowsappsdk.interactiveexperiences 2.1.6
  de7b5907c63c8a79606ccc8f0d98943b154a2e62312308187e8cdc3304ff3d0b)
speecher_fetch_wasdk_package(SPEECHER_WASDK_WINUI
  microsoft.windowsappsdk.winui 2.3.6
  3404f14b5aad656024cf23efab49373e14be4129b1e7d8cdd18dd26712d3c8e2)
speecher_fetch_wasdk_package(SPEECHER_CPPWINRT
  microsoft.windows.cppwinrt 3.0.260818.1
  a8993608ae9263a8288e1a1d9ee2684a5632f4acc59e1e89ddd86b3b889a7367)
speecher_fetch_wasdk_package(SPEECHER_WIL
  microsoft.windows.implementationlibrary 1.0.260126.7
  7d67bc71dd0edc342db44ea0334ac9b6a1776514f81694b0b5b3c7ac9d608e42)
speecher_fetch_wasdk_package(SPEECHER_WEBVIEW2
  microsoft.web.webview2 1.0.3719.77
  2f6be3a10a1a8d6d1fde986af4131dab344f8181fffe75590824e0f4b037ed73)

file(GLOB SPEECHER_WASDK_FOUNDATION_WINMDS
  "${SPEECHER_WASDK_FOUNDATION}/metadata/*.winmd")
file(GLOB SPEECHER_WASDK_INTERACTIVE_WINMDS
  "${SPEECHER_WASDK_INTERACTIVE}/metadata/10.0.17763.0/*.winmd")
set(SPEECHER_WASDK_WINUI_WINMDS
  "${SPEECHER_WASDK_WINUI}/metadata/Microsoft.UI.Xaml.winmd"
  "${SPEECHER_WASDK_WINUI}/metadata/Microsoft.UI.Text.winmd"
)
set(SPEECHER_WASDK_WINMDS
  ${SPEECHER_WASDK_FOUNDATION_WINMDS}
  ${SPEECHER_WASDK_INTERACTIVE_WINMDS}
  ${SPEECHER_WASDK_WINUI_WINMDS}
)
set(SPEECHER_WEBVIEW2_WINMD
  "${SPEECHER_WEBVIEW2}/lib/Microsoft.Web.WebView2.Core.winmd")
set(SPEECHER_WINRT_GEN "${CMAKE_BINARY_DIR}/winrt-gen")
set(SPEECHER_CPPWINRT_EXE "${SPEECHER_CPPWINRT}/bin/cppwinrt.exe")

add_custom_command(
  OUTPUT "${SPEECHER_WINRT_GEN}/winrt/Microsoft.UI.Xaml.h"
  COMMAND "${SPEECHER_CPPWINRT_EXE}"
    -input sdk
    -input ${SPEECHER_WASDK_WINUI_WINMDS}
    -input "${SPEECHER_WASDK_INTERACTIVE}/metadata/10.0.17763.0"
    -input ${SPEECHER_WASDK_FOUNDATION_WINMDS}
    -input "${SPEECHER_WEBVIEW2_WINMD}"
    -output "${SPEECHER_WINRT_GEN}"
    -pch .
  DEPENDS "${SPEECHER_CPPWINRT_EXE}" ${SPEECHER_WASDK_WINMDS}
    "${SPEECHER_WEBVIEW2_WINMD}"
  COMMENT "Generating Windows App SDK C++/WinRT projections"
  COMMAND_EXPAND_LISTS
  VERBATIM
)
add_custom_target(winrt_projection
  DEPENDS "${SPEECHER_WINRT_GEN}/winrt/Microsoft.UI.Xaml.h")

add_library(winappsdk INTERFACE)
add_dependencies(winappsdk winrt_projection)
target_include_directories(winappsdk INTERFACE
  "${SPEECHER_WINRT_GEN}"
  "${SPEECHER_WASDK_FOUNDATION}/include"
  "${SPEECHER_WASDK_INTERACTIVE}/include"
  "${SPEECHER_WASDK_WINUI}/include"
  "${SPEECHER_WIL}/include"
  "${CMAKE_CURRENT_LIST_DIR}"
)
target_link_libraries(winappsdk INTERFACE
  "${SPEECHER_WASDK_FOUNDATION}/lib/native/x64/Microsoft.WindowsAppRuntime.Bootstrap.lib"
  "${SPEECHER_WASDK_INTERACTIVE}/lib/native/x64/Microsoft.UI.Dispatching.lib"
  "${SPEECHER_CPPWINRT}/build/native/lib/x64/cppwinrt_fast_forwarder.lib"
  WindowsApp RuntimeObject delayimp dwmapi shell32 uiautomationcore
)
target_link_options(winappsdk INTERFACE
  "/DELAYLOAD:Microsoft.UI.Windowing.Core.dll")
target_compile_definitions(winappsdk INTERFACE
  UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN WINRT_LEAN_AND_MEAN)
target_compile_options(winappsdk INTERFACE
  /EHsc /bigobj /permissive- /Zc:__cplusplus /Zc:preprocessor)

set(SPEECHER_WASDK_BOOTSTRAP_DLL
  "${SPEECHER_WASDK_FOUNDATION}/runtimes/win-x64/native/Microsoft.WindowsAppRuntime.Bootstrap.dll")
