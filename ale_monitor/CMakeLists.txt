# ale_monitor — standalone-capable CMake build
#
# Works two ways:
#   (A) Subdirectory of openALE root:  cmake -B build && cmake --build build --target ale_monitor
#   (B) Standalone:  cd ale_monitor && cmake -B build && cmake --build build
#
# Sentinel: ale_app_core already defined  ↔  running as subdirectory of root.
# If not, pull in the parent with EXCLUDE_FROM_ALL so only the lib targets we
# need are built — not openALE, tests, etc.

if(NOT TARGET ale_app_core)
    cmake_minimum_required(VERSION 3.15)
    project(ale_monitor VERSION 1.0.0 LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    if(MSVC)
        add_compile_options(/bigobj /W4 /utf-8)
    else()
        if(NOT CMAKE_BUILD_TYPE)
            set(CMAKE_BUILD_TYPE Release)
        endif()
        set(CMAKE_CXX_FLAGS_DEBUG  "-g -O0 -Wall -Wextra")
        set(CMAKE_CXX_FLAGS_RELEASE "-O3")
    endif()
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/.."
                     "${CMAKE_CURRENT_BINARY_DIR}/ale_parent"
                     EXCLUDE_FROM_ALL)
endif()

set(ALE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")

add_executable(ale_monitor
    src/ale_monitor.cpp
    "${ALE_ROOT}/apps/bridge/ws_server.cpp"   # reused directly — not copied
)

target_include_directories(ale_monitor PRIVATE
    "${ALE_ROOT}/apps"    # enables: #include "bridge/ws_server.h"
                          #          #include "bridge/minijson.h"
    # ${ALE_ROOT}/include is NOT listed here: it is already PUBLIC on ale_app_core
    # and propagates transitively — adding it again would just be noise.
)

target_link_libraries(ale_monitor
    ale_app_core
    ale_audio_device
    ale_stores
    ale_pal
)

if(WIN32)
    target_link_libraries(ale_monitor ws2_32)
else()
    # Same reasoning as openALE's -rdynamic in the root CMakeLists.txt: needed
    # for backtrace_symbols() to name in-executable frames in crash.log.
    target_link_options(ale_monitor PRIVATE -rdynamic)
endif()

# Copy Hamlib DLLs next to the exe on Windows (same pattern as openALE in root
# CMakeLists.txt — ale_pal already pulls in Hamlib when found).
if(HAMLIB_FOUND AND DEFINED _hamlib_bin)
    file(GLOB _hamlib_dlls "${_hamlib_bin}/*.dll")
    foreach(_dll ${_hamlib_dlls})
        add_custom_command(TARGET ale_monitor POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${_dll}" "$<TARGET_FILE_DIR:ale_monitor>"
            COMMENT "Copying ${_dll}"
        )
    endforeach()
endif()
