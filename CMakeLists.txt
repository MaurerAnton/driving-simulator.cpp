# SPDX-License-Identifier: AGPL-3.0-only
#
# Fahrsim Baunatal — C++ port (Emscripten/WASM).
#
# Build with Emscripten:
#   emcmake cmake -B build -DCMAKE_BUILD_TYPE=Release
#   cmake --build build
# The result is build/fahrsim.js + build/fahrsim.wasm.
#
# Native build (for tests/CI smoke): plain cmake, produces a console binary.

cmake_minimum_required(VERSION 3.20)
project(fahrsim-cpp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(FAHRSIM_WASM "Build the WebAssembly target (Emscripten)" ${EMSCRIPTEN})

# --- sources -----------------------------------------------------------------
# Grown incrementally: engine/ (three.js equivalent), game/ (the simulator),
# app/ (entry point).  Each file carries "// Origin: src/…".
file(GLOB_RECURSE FAHRSIM_SOURCES CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")

# everything except the entry point (the entry point is only for the executable)
list(FILTER FAHRSIM_SOURCES EXCLUDE REGEX "src/app/main\\.cpp$")
set(FAHRSIM_APP_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/src/app/main.cpp")

# game sources include headers as "game/…", "engine/…", …
set(FAHRSIM_INCLUDE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/src")

if(FAHRSIM_WASM)
  add_executable(fahrsim ${FAHRSIM_APP_SOURCE} ${FAHRSIM_SOURCES})
  target_include_directories(fahrsim PRIVATE ${FAHRSIM_INCLUDE_DIRS})
  target_compile_options(fahrsim PRIVATE -O2)
  set_target_properties(fahrsim PROPERTIES OUTPUT_NAME "fahrsim" SUFFIX ".js")
  target_link_options(fahrsim PRIVATE
    "SHELL:-s WASM=1"
    "SHELL:-s MODULARIZE=0"
    "SHELL:-s EXPORTED_RUNTIME_METHODS=['ccall','cwrap']"
    "SHELL:-s ALLOW_MEMORY_GROWTH=1"
    "SHELL:-s INITIAL_MEMORY=67108864"
    "SHELL:-s STACK_SIZE=8388608"
    "SHELL:-s ENVIRONMENT=web,worker"
    "SHELL:-s FORCE_FILESYSTEM=1"
    "SHELL:-s EXIT_RUNTIME=0"
    "SHELL:-s EXPORTED_FUNCTIONS=['_main']"
    "SHELL:-s ASSERTIONS=1")
else()
  add_executable(fahrsim ${FAHRSIM_APP_SOURCE} ${FAHRSIM_SOURCES})
  target_include_directories(fahrsim PRIVATE ${FAHRSIM_INCLUDE_DIRS})
  target_compile_options(fahrsim PRIVATE -O2 -Wall -Wextra)

  # unit tests (also run in CI before the WASM job)
  enable_testing()
  file(GLOB FAHRSIM_TESTS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp")
  foreach(t IN LISTS FAHRSIM_TESTS)
    get_filename_component(tname "${t}" NAME_WE)
    add_executable(${tname} "${t}")
    # tests link the game sources they exercise (the same .cpp files)
    target_sources(${tname} PRIVATE ${FAHRSIM_SOURCES})
    target_include_directories(${tname} PRIVATE ${FAHRSIM_INCLUDE_DIRS} "${CMAKE_CURRENT_SOURCE_DIR}")
    target_compile_options(${tname} PRIVATE -O1 -Wall -Wextra)
    add_test(NAME ${tname} COMMAND ${tname})
  endforeach()
endif()
