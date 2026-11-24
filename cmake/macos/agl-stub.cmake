# Qt6's WrapOpenGL link interface still references AGL.framework, which the
# Xcode 26 macOS SDK no longer ships (the copy in /System/Library/Frameworks
# is a metadata-only shell). Nothing here calls AGL, so link against an empty
# stub instead. Used by the plugin and by tests/ui.

include_guard(GLOBAL)

function(langlive_link_agl_stub target)
  set(_agl_stub_dir "${CMAKE_BINARY_DIR}/agl-stub-frameworks")
  set(_agl_stub_dylib "${_agl_stub_dir}/AGL.framework/Versions/A/AGL")
  if(NOT EXISTS "${_agl_stub_dylib}")
    file(MAKE_DIRECTORY "${_agl_stub_dir}/AGL.framework/Versions/A")
    execute_process(
      COMMAND xcrun --sdk macosx --show-sdk-path
      OUTPUT_VARIABLE _agl_stub_sysroot
      OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    execute_process(
      COMMAND
        ${CMAKE_C_COMPILER} -isysroot "${_agl_stub_sysroot}" -arch arm64 -arch x86_64 -dynamiclib -install_name
        /System/Library/Frameworks/AGL.framework/Versions/A/AGL -o "${_agl_stub_dylib}" -x c /dev/null
      RESULT_VARIABLE _agl_stub_result
    )
    if(NOT _agl_stub_result EQUAL 0)
      message(FATAL_ERROR "Failed to build AGL.framework stub")
    endif()
    file(CREATE_LINK "A" "${_agl_stub_dir}/AGL.framework/Versions/Current" SYMBOLIC)
    file(CREATE_LINK "Versions/Current/AGL" "${_agl_stub_dir}/AGL.framework/AGL" SYMBOLIC)
  endif()
  # The stub only satisfies the linker. Drop libraries nothing uses, so the
  # binary does not load AGL.framework at runtime: Apple deprecated it and
  # newer SDKs no longer ship it, so a future macOS may not have it.
  target_link_options(${target} PRIVATE "-F${_agl_stub_dir}" "-Wl,-dead_strip_dylibs")
endfunction()
