# CMake build number module

include_guard(GLOBAL)

# Define build number cache file
set(
  _BUILD_NUMBER_CACHE
  "${CMAKE_CURRENT_SOURCE_DIR}/cmake/.CMakeBuildNumber"
  CACHE INTERNAL
  "OBS build number cache file"
)

# Read build number from cache file or manual override
if(NOT DEFINED PLUGIN_BUILD_NUMBER)
  if(EXISTS "${_BUILD_NUMBER_CACHE}")
    file(READ "${_BUILD_NUMBER_CACHE}" PLUGIN_BUILD_NUMBER)
    math(EXPR PLUGIN_BUILD_NUMBER "${PLUGIN_BUILD_NUMBER}+1")
  elseif(DEFINED ENV{CI_PIPELINE_IID})
    # GitLab CI: per-project pipeline number
    set(PLUGIN_BUILD_NUMBER "$ENV{CI_PIPELINE_IID}")
  elseif(DEFINED ENV{GITHUB_RUN_NUMBER})
    set(PLUGIN_BUILD_NUMBER "$ENV{GITHUB_RUN_NUMBER}")
  else()
    set(PLUGIN_BUILD_NUMBER "1")
  endif()
  file(WRITE "${_BUILD_NUMBER_CACHE}" "${PLUGIN_BUILD_NUMBER}")
endif()
