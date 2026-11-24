include(FetchContent)

# doctest is header-only. Its own CMakeLists.txt predates CMake 4, so only
# download the sources (SOURCE_SUBDIR points at a directory without one) and
# expose the header through an interface target.
FetchContent_Declare(
  doctest
  URL https://github.com/doctest/doctest/archive/refs/tags/v2.4.11.tar.gz
  URL_HASH SHA256=632ed2c05a7f53fa961381497bf8069093f0d6628c5f26286161fbd32a560186
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  SOURCE_SUBDIR do-not-add
)
FetchContent_MakeAvailable(doctest)

add_library(doctest INTERFACE)
add_library(doctest::doctest ALIAS doctest)
target_include_directories(doctest SYSTEM INTERFACE "${doctest_SOURCE_DIR}")

add_executable(langlive-core-tests)
target_sources(
  langlive-core-tests
  PRIVATE main.cpp phone_test.cpp redaction_test.cpp stream_spec_test.cpp update_test.cpp validation_test.cpp version_test.cpp website_test.cpp
)
target_link_libraries(langlive-core-tests PRIVATE langlive::obsplugin::core doctest::doctest)

add_test(NAME langlive-core-tests COMMAND langlive-core-tests)
