# langlive-core: platform-independent logic of the LangLive OBS Plugin.
# No dependency on libobs or Qt, so it can be built and unit-tested on its
# own (tests/CMakeLists.txt runs these together with the app tests):
#
#   cmake -S core -B core/build -DLANGLIVE_CORE_BUILD_TESTS=ON
#   cmake --build core/build
#   ctest --test-dir core/build --output-on-failure

cmake_minimum_required(VERSION 3.28)

project(langlive-core LANGUAGES CXX)

option(LANGLIVE_CORE_BUILD_TESTS "Build langlive-core unit tests" OFF)

add_library(langlive-core STATIC)
add_library(langlive::obsplugin::core ALIAS langlive-core)

target_sources(
  langlive-core
  PRIVATE
    src/phone.cpp
    src/redaction.cpp
    src/stream_spec.cpp
    src/update.cpp
    src/validation.cpp
    src/version.cpp
    src/website.cpp
  PUBLIC
    FILE_SET HEADERS
    BASE_DIRS include
    FILES
      include/langlive/obsplugin/core/phone.hpp
      include/langlive/obsplugin/core/redaction.hpp
      include/langlive/obsplugin/core/stream_spec.hpp
      include/langlive/obsplugin/core/update.hpp
      include/langlive/obsplugin/core/validation.hpp
      include/langlive/obsplugin/core/version.hpp
      include/langlive/obsplugin/core/website.hpp
)

target_compile_features(langlive-core PUBLIC cxx_std_17)
set_target_properties(langlive-core PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(LANGLIVE_CORE_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
