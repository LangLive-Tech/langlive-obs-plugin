# Puts the OBS dependency archives listed in buildspec.json into .deps/ before
# the plugin's configure step, which then uses them instead of downloading.
#
#   cmake -DPLATFORM=macos|windows-x64 -P scripts/ci/prefetch-deps.cmake
#
# OBS_DEPS_MIRROR (environment, optional): a server holding the archives at
# <mirror>/<GitHub repository>/<file> for obs-studio and
# <mirror>/<GitHub repository>/<version>/<file> for obs-deps, the same paths as
# on GitHub. Without it, or when the mirror fails, the configure step downloads
# from GitHub as usual.
#
# Archives already in .deps/ are checked against buildspec.json too: the
# configure step checks the hash only while downloading, so an archive cut
# short by a canceled job would otherwise fail every later build.

cmake_minimum_required(VERSION 3.20)

if(NOT PLATFORM MATCHES "^(macos|windows-x64)$")
  message(FATAL_ERROR "Set -DPLATFORM=macos or -DPLATFORM=windows-x64")
endif()

get_filename_component(project_root "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(deps_dir "${project_root}/.deps")
file(MAKE_DIRECTORY "${deps_dir}")
file(READ "${project_root}/buildspec.json" buildspec)
string(JSON dependencies GET "${buildspec}" dependencies)

# The archive names used by cmake/macos/buildspec.cmake and cmake/windows/buildspec.cmake.
if(PLATFORM STREQUAL macos)
  set(arch universal)
  set(prebuilt_filename "macos-deps-VERSION-ARCH_REVISION.tar.xz")
  set(qt6_filename "macos-deps-qt6-VERSION-ARCH-REVISION.tar.xz")
  set(obs-studio_filename "VERSION.tar.gz")
else()
  set(arch x64)
  set(prebuilt_filename "windows-deps-VERSION-ARCH-REVISION.zip")
  set(qt6_filename "windows-deps-qt6-VERSION-ARCH-REVISION.zip")
  set(obs-studio_filename "VERSION.zip")
endif()

set(mirror "$ENV{OBS_DEPS_MIRROR}")
string(REGEX REPLACE "/+$" "" mirror "${mirror}")
if(mirror)
  message(STATUS "OBS dependency mirror: ${mirror}")
else()
  message(STATUS "No OBS_DEPS_MIRROR: the configure step downloads missing archives from GitHub")
endif()

foreach(dependency IN ITEMS prebuilt qt6 obs-studio)
  string(JSON data GET "${dependencies}" ${dependency})
  string(JSON version GET "${data}" version)
  string(JSON hash GET "${data}" hashes ${PLATFORM})
  string(JSON base_url GET "${data}" baseUrl)
  string(JSON revision ERROR_VARIABLE no_revision GET "${data}" revision ${PLATFORM})

  set(file "${${dependency}_filename}")
  string(REPLACE "VERSION" "${version}" file "${file}")
  string(REPLACE "ARCH" "${arch}" file "${file}")
  if(revision AND NOT no_revision)
    string(REPLACE "_REVISION" "_v${revision}" file "${file}")
    string(REPLACE "-REVISION" "-v${revision}" file "${file}")
  else()
    string(REPLACE "_REVISION" "" file "${file}")
    string(REPLACE "-REVISION" "" file "${file}")
  endif()
  set(archive "${deps_dir}/${file}")

  if(EXISTS "${archive}")
    file(SHA256 "${archive}" actual)
    if(actual STREQUAL hash)
      message(STATUS "${file}: present, hash ok")
      continue()
    endif()
    message(WARNING "${file}: wrong hash (an interrupted download?); removing it")
    file(REMOVE "${archive}")
  endif()

  # Extracted by an earlier configure: it does not need the archive again.
  set(marker "${deps_dir}/.dependency_${dependency}_${arch}.sha256")
  if(NOT dependency STREQUAL obs-studio AND EXISTS "${marker}")
    file(READ "${marker}" extracted_hash)
    if(extracted_hash STREQUAL hash)
      message(STATUS "${file}: already extracted")
      continue()
    endif()
  endif()

  if(NOT mirror)
    continue()
  endif()

  # https://github.com/obsproject/<repository>/... -> <mirror>/<repository>/
  if(NOT base_url MATCHES "^https://github\\.com/[^/]+/([^/]+)/")
    message(WARNING "${dependency}: baseUrl is not on GitHub; not mirrored")
    continue()
  endif()
  set(url "${mirror}/${CMAKE_MATCH_1}")
  if(NOT dependency STREQUAL obs-studio)
    string(APPEND url "/${version}")
  endif()
  string(APPEND url "/${file}")

  message(STATUS "Downloading ${url}")
  string(TIMESTAMP started "%s")
  # The hash is checked here: with EXPECTED_HASH a mismatch is a fatal
  # error, and a bad mirror must only fall back to GitHub.
  file(DOWNLOAD "${url}" "${archive}.part" STATUS status INACTIVITY_TIMEOUT 30)
  list(GET status 0 code)
  list(GET status 1 reason)
  if(code EQUAL 0)
    file(SHA256 "${archive}.part" actual)
    if(NOT actual STREQUAL hash)
      set(code 1)
      set(reason "wrong hash")
    endif()
  endif()
  if(code EQUAL 0)
    file(RENAME "${archive}.part" "${archive}")
    string(TIMESTAMP finished "%s")
    math(EXPR seconds "${finished} - ${started}")
    message(STATUS "Downloading ${url} - done (${seconds} s)")
  else()
    file(REMOVE "${archive}.part")
    message(WARNING "Downloading ${url} failed (${reason}); the configure step downloads it from GitHub")
  endif()
endforeach()
