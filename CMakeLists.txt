# SPDX-License-Identifier: AGPL-3.0-only
#
# Fahrsim Baunatal 2 — the C++ port (WebAssembly in the browser, native for development).
#
#   Browser (Emscripten):   emcmake cmake -B build-web -G Ninja && cmake --build build-web
#                           → build-web/site/  (index.html, fahrsim.js, fahrsim.wasm, …)
#   Native (tests, tools):  cmake -B build -G Ninja && cmake --build build && ctest --test-dir build
#
# See docs/PORT.md for how the port is laid out and how to work on it.

cmake_minimum_required(VERSION 3.20)
project(fahrsim-cpp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(FAHRSIM_WASM "Build the WebAssembly target (Emscripten)" ${EMSCRIPTEN})
# how hard the compiler works: the native build is for tests and tools (quick to build),
# the browser build is what players run
if(FAHRSIM_WASM)
  set(FAHRSIM_OPT "-O2" CACHE STRING "optimisation level")
else()
  set(FAHRSIM_OPT "-O1" CACHE STRING "optimisation level")
endif()

find_program(CCACHE ccache)
if(CCACHE AND NOT CMAKE_CXX_COMPILER_LAUNCHER)
  set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE}")
endif()

# --- sources -----------------------------------------------------------------
# src/<dir>/<name>.{h,cpp} mirror the original's src/<dir>/<name>.ts.  What only one
# host has lives in src/platform/web (the browser) and src/platform/native (this machine);
# the two entry points are src/app/main_web.cpp and src/app/main_native.cpp.
file(GLOB_RECURSE FAHRSIM_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
list(FILTER FAHRSIM_SOURCES EXCLUDE REGEX "/src/app/")
set(FAHRSIM_WEB_SOURCES ${FAHRSIM_SOURCES})
list(FILTER FAHRSIM_WEB_SOURCES INCLUDE REGEX "/src/platform/web/")
set(FAHRSIM_NATIVE_SOURCES ${FAHRSIM_SOURCES})
list(FILTER FAHRSIM_NATIVE_SOURCES INCLUDE REGEX "/src/platform/native/")
list(FILTER FAHRSIM_SOURCES EXCLUDE REGEX "/src/platform/(web|native)/")

set(FAHRSIM_INCLUDE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/src")
set(FAHRSIM_WARN -Wall -Wextra -Wno-unused-parameter)
# the standard headers nearly every file includes, parsed once
set(FAHRSIM_PCH <algorithm> <array> <cmath> <cstdint> <cstdio> <cstdlib> <cstring> <functional>
    <map> <memory> <optional> <set> <string> <unordered_map> <unordered_set> <utility> <vector>)

if(FAHRSIM_WASM)
  add_executable(fahrsim "${CMAKE_CURRENT_SOURCE_DIR}/src/app/main_web.cpp" ${FAHRSIM_SOURCES} ${FAHRSIM_WEB_SOURCES})
  target_include_directories(fahrsim PRIVATE ${FAHRSIM_INCLUDE_DIRS})
  target_compile_options(fahrsim PRIVATE ${FAHRSIM_OPT} ${FAHRSIM_WARN} -fwasm-exceptions -sUSE_ZLIB=1)
  target_precompile_headers(fahrsim PRIVATE ${FAHRSIM_PCH})
  set(FAHRSIM_SITE "${CMAKE_BINARY_DIR}/site")
  file(MAKE_DIRECTORY "${FAHRSIM_SITE}")
  set_target_properties(fahrsim PROPERTIES OUTPUT_NAME "fahrsim" SUFFIX ".js"
                        RUNTIME_OUTPUT_DIRECTORY "${FAHRSIM_SITE}")
  target_link_options(fahrsim PRIVATE
    ${FAHRSIM_OPT} -fwasm-exceptions -sUSE_ZLIB=1
    "SHELL:-s WASM=1"
    "SHELL:-s MIN_WEBGL_VERSION=2"
    "SHELL:-s MAX_WEBGL_VERSION=2"
    "SHELL:-s ALLOW_MEMORY_GROWTH=1"
    "SHELL:-s INITIAL_MEMORY=134217728"
    "SHELL:-s MAXIMUM_MEMORY=2147483648"
    "SHELL:-s STACK_SIZE=4194304"
    "SHELL:-s ENVIRONMENT=web"
    "SHELL:-s EXIT_RUNTIME=0"
    "SHELL:-s EXPORTED_RUNTIME_METHODS=['UTF8ToString','stringToUTF8','lengthBytesUTF8','HEAPU8','HEAPF32','HEAPF64','HEAP32']"
    "SHELL:-s EXPORTED_FUNCTIONS=['_main','_malloc','_free']"
    "SHELL:-s ASSERTIONS=0"
    "SHELL:--js-library ${CMAKE_CURRENT_SOURCE_DIR}/src/platform/web/library_fahrsim.js")
  # the page around the module: copied beside it
  add_custom_command(TARGET fahrsim POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/web" "${FAHRSIM_SITE}"
    COMMENT "the page (web/) beside the module")
else()
  # Native: each directory under src/ is one static library (fs_world, fs_sim …), every source
  # compiled once.  No NDEBUG: assert() is live everywhere in the native build.
  enable_testing()
  add_compile_options(${FAHRSIM_OPT} ${FAHRSIM_WARN})
  include_directories(${FAHRSIM_INCLUDE_DIRS})
  find_package(ZLIB REQUIRED)
  # the native host draws with OpenGL ES 3 through EGL (off screen), for pictures without a browser
  find_package(PkgConfig)
  if(PkgConfig_FOUND)
    pkg_check_modules(FS_EGL IMPORTED_TARGET egl glesv2)
    pkg_check_modules(FS_PNG IMPORTED_TARGET libpng)
  endif()
  if(FS_EGL_FOUND)
    add_compile_definitions(FS_NATIVE_GL=1)
  endif()
  if(FS_PNG_FOUND)
    add_compile_definitions(FS_HAVE_PNG=1)
  endif()

  # the precompiled header every library and test shares
  file(WRITE "${CMAKE_BINARY_DIR}/fs_pch.cpp" "// the standard headers, parsed once\n")
  add_library(fs_pch STATIC "${CMAKE_BINARY_DIR}/fs_pch.cpp")
  target_precompile_headers(fs_pch PRIVATE ${FAHRSIM_PCH})

  set(FS_LAYERS "")
  file(GLOB FS_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/src/*")
  foreach(d IN LISTS FS_DIRS)
    get_filename_component(layer "${d}" NAME)
    if(IS_DIRECTORY "${d}" AND NOT layer STREQUAL "app")
      file(GLOB_RECURSE layer_sources CONFIGURE_DEPENDS "${d}/*.cpp")
      list(FILTER layer_sources EXCLUDE REGEX "/src/platform/web/")
      if(layer_sources)
        add_library(fs_${layer} STATIC ${layer_sources})
        target_precompile_headers(fs_${layer} REUSE_FROM fs_pch)
        list(APPEND FS_LAYERS fs_${layer})
      endif()
    endif()
  endforeach()
  set(FS_SYSTEM_LIBS ZLIB::ZLIB)
  if(FS_EGL_FOUND)
    list(APPEND FS_SYSTEM_LIBS PkgConfig::FS_EGL)
  endif()
  if(FS_PNG_FOUND)
    list(APPEND FS_SYSTEM_LIBS PkgConfig::FS_PNG)
  endif()
  # link these layers (all of them when none are named), whatever order they need each other in
  function(fs_link target)
    set(libs ${ARGN})
    if(NOT libs)
      set(libs ${FS_LAYERS})
    endif()
    string(REPLACE ";" "," group "${libs}")
    target_link_libraries(${target} PRIVATE "$<LINK_GROUP:RESCAN,${group}>" ${FS_SYSTEM_LIBS})
    target_precompile_headers(${target} REUSE_FROM fs_pch)
  endfunction()

  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/app/main_native.cpp")
    add_executable(fahrsim "${CMAKE_CURRENT_SOURCE_DIR}/src/app/main_native.cpp")
    fs_link(fahrsim)
  endif()

  # tests/<name>_test.cpp → one executable each;  tools/dev/<name>.cpp → one tool each.
  # Built only when asked for (tools/t.sh <name> builds and runs one, tools/check.sh all).
  # A line "// LINK: world core platform" in the file names the layers it needs; without it, all.
  file(GLOB FAHRSIM_TESTS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tests/*_test.cpp")
  file(GLOB FAHRSIM_TOOLS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tools/dev/*.cpp")
  # tests known to fail (kept visible: each is a fault to be found, see tests/known-failing.txt)
  set(FAHRSIM_KNOWN_FAILING "")
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/known-failing.txt")
    file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/tests/known-failing.txt" FAHRSIM_KNOWN_FAILING)
  endif()
  set(FAHRSIM_ALL_TESTS "")
  foreach(t IN LISTS FAHRSIM_TESTS FAHRSIM_TOOLS)
    get_filename_component(tname "${t}" NAME_WE)
    add_executable(${tname} EXCLUDE_FROM_ALL "${t}")
    target_include_directories(${tname} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    set(wanted "")
    file(STRINGS "${t}" link_line REGEX "^// LINK:" LIMIT_COUNT 1)
    if(link_line)
      string(REGEX REPLACE "^// LINK:[ ]*" "" link_line "${link_line}")
      string(REGEX REPLACE "[ ]+" ";" link_names "${link_line}")
      foreach(n IN LISTS link_names)
        if(TARGET fs_${n})
          list(APPEND wanted fs_${n})
        endif()
      endforeach()
    endif()
    fs_link(${tname} ${wanted})
    if(t MATCHES "/tests/")
      list(APPEND FAHRSIM_ALL_TESTS ${tname})
      add_test(NAME ${tname} COMMAND ${tname} WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
      list(FIND FAHRSIM_KNOWN_FAILING ${tname} known)
      if(NOT known EQUAL -1)
        set_tests_properties(${tname} PROPERTIES DISABLED TRUE)
      endif()
    endif()
  endforeach()
  add_custom_target(tests DEPENDS ${FAHRSIM_ALL_TESTS})
endif()
