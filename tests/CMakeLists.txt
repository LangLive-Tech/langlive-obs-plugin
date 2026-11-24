# Unit tests that build without OBS or Qt: langlive-core and the
# application layer (src/app). Entry point for local runs and CI:
#
#   cmake -S tests -B build_tests
#   cmake --build build_tests
#   ctest --test-dir build_tests --output-on-failure

cmake_minimum_required(VERSION 3.28)

project(langlive-obs-plugin-tests LANGUAGES CXX)

enable_testing()

# Also builds the core tests and provides doctest::doctest.
set(LANGLIVE_CORE_BUILD_TESTS ON CACHE BOOL "" FORCE)
add_subdirectory(../core core)

set(_src_dir "${CMAKE_CURRENT_SOURCE_DIR}/../src")

add_executable(langlive-app-tests)
target_sources(
  langlive-app-tests
  PRIVATE
    app/fakes.hpp
    app/main.cpp
    app/mock_langlive_api_test.cpp
    app/plugin_controller_test.cpp
    ${_src_dir}/app/langlive-api.cpp
    ${_src_dir}/app/langlive-api.hpp
    ${_src_dir}/app/mock-langlive-api.cpp
    ${_src_dir}/app/mock-langlive-api.hpp
    ${_src_dir}/app/plugin-controller.cpp
    ${_src_dir}/app/plugin-controller.hpp
    ${_src_dir}/app/ports.hpp
)
target_include_directories(langlive-app-tests PRIVATE "${_src_dir}")
target_link_libraries(langlive-app-tests PRIVATE langlive::obsplugin::core doctest::doctest)

add_test(NAME langlive-app-tests COMMAND langlive-app-tests)
