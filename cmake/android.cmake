# SPDX-License-Identifier: AGPL-3.0-only
# The Android build (experimental – docs/SDL.md, "Android"): fahrsim-sdl as the library a
# NativeActivity loads, libmain.so.  What differs from the desktop's SDL build is said here.
#
# After progressive-chat's cmake/android.cmake (the same author, AGPL-3.0-only): the same fork
# of SDL3 at the same commit, with the same options.  progressive-chat asks for it through
# cmake-everywhere; here plain FetchContent does (the workflow hands it the checkout it has
# already made: -DFETCHCONTENT_SOURCE_DIR_SDL3=…).  And where progressive-chat builds an NDK of
# its own (it needs clang 23 for `import std`), this C++26 game (no modules yet) is built with the NDK the
# runner has.
#
#   cmake -S . -B build-android -G Ninja \
#     -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK/build/cmake/android.toolchain.cmake \
#     -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-26 -DFS_SDL3=ON
#   cmake --build build-android -t fahrsim-sdl          → build-android/libmain.so
#   tools/android/apk.sh …                              → the APK (see .github/workflows/android.yml)

include(FetchContent)

# The entry a NativeActivity needs: Android starts the activity itself, and SDL3 bridges it
# to the program.  Upstream has released no SDL3 that carries that bridge, so this build asks
# for the fork that does (j4niwzis/SDL), and gives it the NDK's own native_app_glue.
set(FS_ANDROID_NATIVE_APP_GLUE "${ANDROID_NDK}/sources/android/native_app_glue"
  CACHE PATH "the NDK's native_app_glue (android_native_app_glue.c)")
set(SDL_ANDROID_NATIVE_ACTIVITY ON CACHE BOOL "" FORCE)
set(SDL_ANDROID_NATIVE_APP_GLUE "${FS_ANDROID_NATIVE_APP_GLUE}" CACHE PATH "" FORCE)
set(SDL_ANDROID_JAR OFF CACHE BOOL "" FORCE)
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
set(SDL_VULKAN OFF CACHE BOOL "" FORCE)
# (nothing here opens a camera; the fork's native profile has neither controllers nor haptics)
set(SDL_CAMERA OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_Declare(sdl3
  URL https://github.com/j4niwzis/SDL/archive/91c08de87b43fdecc824464fcce15499ccbc7101.tar.gz)
FetchContent_MakeAvailable(sdl3)

# the host: src/platform/sdl (with android_net.cpp), as on the desktop
file(GLOB FS_SDL_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/sdl/*.cpp")
add_library(fs_sdl STATIC ${FS_SDL_SOURCES})
target_precompile_headers(fs_sdl REUSE_FROM fs_pch)
target_link_libraries(fs_sdl PUBLIC SDL3::SDL3)

# The program as the library the activity loads: android.app.lib_name "main" in the manifest,
# and the dex's NativeActivity loads it by that name (its JNI_OnLoad is SDL's, linked in here).
add_library(fahrsim-sdl SHARED "${CMAKE_CURRENT_SOURCE_DIR}/src/app/main_sdl.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/src/app/main_android.cpp")
set_target_properties(fahrsim-sdl PROPERTIES OUTPUT_NAME main)
target_compile_definitions(fahrsim-sdl PRIVATE "FS_SOURCE_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}\"")
fs_link(fahrsim-sdl fs_sdl ${FS_LAYERS})
target_link_libraries(fahrsim-sdl PRIVATE SDL3::SDL3 android log)
# no symbol may stay unresolved in a library ART loads; pages of 16 KB (Android 15's devices)
target_link_options(fahrsim-sdl PRIVATE -Wl,--no-undefined -Wl,-z,max-page-size=16384)
