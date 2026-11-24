cmake_minimum_required(VERSION 3.28...3.30)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(${_name} VERSION ${_version})

option(ENABLE_FRONTEND_API "Use obs-frontend-api for UI functionality" ON)
option(ENABLE_QT "Use Qt functionality" ON)

# Update manifest (latest.json). Must be HTTPS; http://127.0.0.1 is allowed
# for a local test server (scripts/dev/serve-update-manifest.py).
# TODO: production default once the release bucket / CDN URL is decided.
set(LANGLIVE_UPDATE_MANIFEST_URL "" CACHE STRING "Update manifest URL (latest.json)")
# The LangLive API domain ({DOMAIN} in the API documentation). Empty uses the
# built-in mock backend. QA and production values come from CI variables.
set(LANGLIVE_API_BASE_URL "" CACHE STRING "LangLive API base URL")
# The LangLive website, for the terms of service and privacy policy links.
# Empty uses production; QA builds set the QA website through a CI variable.
set(LANGLIVE_WEBSITE_URL "" CACHE STRING "LangLive website URL (empty: production)")
# QA builds (the dev snapshot and the test environment) let QA override the
# update manifest (qa-override.json). Never on for production builds.
option(LANGLIVE_QA_BUILD "Build with QA-only features" OFF)

if(NOT ENABLE_FRONTEND_API OR NOT ENABLE_QT)
  message(FATAL_ERROR "${_name} requires ENABLE_FRONTEND_API and ENABLE_QT")
endif()

include(compilerconfig)
include(defaults)
include(helpers)

# Platform-independent logic, no libobs / Qt dependency (see core/).
add_subdirectory(core)

# Third-party QR code generator (MIT, see third_party/qrcodegen). Built
# without our warning flags: it is vendored unmodified.
add_library(qrcodegen STATIC third_party/qrcodegen/qrcodegen.cpp third_party/qrcodegen/qrcodegen.hpp)
target_include_directories(qrcodegen SYSTEM PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/third_party/qrcodegen")
target_compile_features(qrcodegen PUBLIC cxx_std_17)
target_compile_options(qrcodegen PRIVATE -w)
set_target_properties(qrcodegen PROPERTIES POSITION_INDEPENDENT_CODE ON COMPILE_WARNING_AS_ERROR OFF)

add_library(${CMAKE_PROJECT_NAME} MODULE)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE langlive::obsplugin::core qrcodegen)
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_definitions(
  ${CMAKE_PROJECT_NAME}
  PRIVATE LANGLIVE_UPDATE_MANIFEST_URL="${LANGLIVE_UPDATE_MANIFEST_URL}" LANGLIVE_API_BASE_URL="${LANGLIVE_API_BASE_URL}"
          LANGLIVE_WEBSITE_URL="${LANGLIVE_WEBSITE_URL}"
          LANGLIVE_QA_BUILD=$<BOOL:${LANGLIVE_QA_BUILD}>
)

find_package(libobs REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::libobs)

find_package(obs-frontend-api REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::obs-frontend-api)

find_package(Qt6 REQUIRED COMPONENTS Widgets Core)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE Qt6::Core Qt6::Widgets)

# HTTPS with libcurl, as OBS itself does: OBS ships no Qt TLS backend, so Qt
# Network cannot make HTTPS requests. macOS uses the system libcurl, Windows
# the one in obs-deps, which OBS installs next to obs.exe.
find_package(CURL REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE CURL::libcurl)
target_compile_options(
  ${CMAKE_PROJECT_NAME}
  PRIVATE $<$<C_COMPILER_ID:Clang,AppleClang>:-Wno-quoted-include-in-framework-header -Wno-comma>
)
set_target_properties(${CMAKE_PROJECT_NAME} PROPERTIES AUTOMOC ON AUTOUIC ON AUTORCC ON)

if(OS_MACOS)
  include(cmake/macos/agl-stub.cmake)
  langlive_link_agl_stub(${CMAKE_PROJECT_NAME})
endif()

# The login token in the system's secure storage: the keychain (macOS),
# Credential Manager (Windows).
if(OS_MACOS)
  target_sources(${CMAKE_PROJECT_NAME} PRIVATE src/obs/secure-store-macos.cpp)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE "-framework Security" "-framework CoreFoundation")
elseif(OS_WINDOWS)
  target_sources(${CMAKE_PROJECT_NAME} PRIVATE src/obs/secure-store-windows.cpp)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE Advapi32)
endif()

target_sources(
  ${CMAKE_PROJECT_NAME}
  PRIVATE
    src/plugin-main.cpp
    src/app/langlive-api.cpp
    src/app/langlive-api.hpp
    src/app/mock-langlive-api.cpp
    src/app/mock-langlive-api.hpp
    src/app/plugin-controller.cpp
    src/app/plugin-controller.hpp
    src/app/ports.hpp
    src/obs/curl-http.cpp
    src/obs/curl-http.hpp
    src/obs/diagnostics.cpp
    src/obs/diagnostics.hpp
    src/obs/http-langlive-api.cpp
    src/obs/http-langlive-api.hpp
    src/obs/langlive-api-json.cpp
    src/obs/qa-override.cpp
    src/obs/qa-override.hpp
    src/obs/langlive-api-json.hpp
    src/obs/obs-settings-store.cpp
    src/obs/obs-settings-store.hpp
    src/obs/obs-streaming-host.cpp
    src/obs/obs-streaming-host.hpp
    src/obs/reconnect-watcher.cpp
    src/obs/reconnect-watcher.hpp
    src/obs/secure-store.hpp
    src/obs/start-failure-watcher.cpp
    src/obs/start-failure-watcher.hpp
    src/obs/update-feed.cpp
    src/obs/update-feed.hpp
    src/ui/cover-picker.cpp
    src/ui/cover-picker.hpp
    src/ui/diagnostics-dialog.cpp
    src/ui/diagnostics-dialog.hpp
    src/ui/go-live-dialog.cpp
    src/ui/go-live-dialog.hpp
    src/ui/langlive-menu.cpp
    src/ui/langlive-menu.hpp
    src/ui/login-dialog.cpp
    src/ui/login-dialog.hpp
    src/ui/message-dialog.cpp
    src/ui/message-dialog.hpp
    src/ui/preflight-dialog.cpp
    src/ui/preflight-dialog.hpp
    src/ui/qr-code-widget.cpp
    src/ui/qr-code-widget.hpp
    src/ui/resources/ui.qrc
    src/ui/style.cpp
    src/ui/style.hpp
    src/ui/text.cpp
    src/ui/text.hpp
    src/ui/update-dialog.cpp
    src/ui/update-dialog.hpp
)

set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})

# License texts travel with the plugin (GPL: the license must accompany
# every copy): macOS bundle Contents/Resources/licenses, Windows
# <plugin>/data/licenses. The installers show no license page, to keep their
# steps short; the GPL does not require accepting it to use the plugin.
set(_license_files "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE" "${CMAKE_CURRENT_SOURCE_DIR}/THIRD-PARTY-NOTICES.txt")
if(OS_MACOS)
  target_sources(${CMAKE_PROJECT_NAME} PRIVATE ${_license_files})
  set_source_files_properties(${_license_files} PROPERTIES MACOSX_PACKAGE_LOCATION Resources/licenses)
elseif(OS_WINDOWS)
  install(FILES ${_license_files} DESTINATION "${CMAKE_PROJECT_NAME}/data/licenses")
endif()
