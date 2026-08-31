cmake_minimum_required(VERSION 3.15)
project(openALE VERSION 0.0.4 LANGUAGES CXX C)
set(OPENALE_VERSION_SUFFIX "-pre-alpha")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    # MSVC sets /Od /Zi (Debug) and /O2 /DNDEBUG (Release) automatically per build type.
    # /bigobj is required for large translation units; /W4 enables high-level warnings.
    # /utf-8 sets both source and execution character sets to UTF-8 (suppresses C4566).
    add_compile_options(/bigobj /W4 /utf-8)
else()
    set(CMAKE_CXX_FLAGS_DEBUG  "-g -O0 -Wall -Wextra")
    set(CMAKE_CXX_FLAGS_RELEASE "-O3")
endif()

# Set default build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Detect platform
if(WIN32)
    set(PLATFORM "windows")
elseif(APPLE)
    set(PLATFORM "macos")
else()
    set(PLATFORM "linux")
endif()

message(STATUS "Building for ${PLATFORM}")

# Optional clang-tidy pass (opt-in: cmake -DENABLE_CLANG_TIDY=ON ..)
option(ENABLE_CLANG_TIDY "Run clang-tidy on every translation unit during build" OFF)
if(ENABLE_CLANG_TIDY)
    find_program(CLANG_TIDY_EXE NAMES clang-tidy)
    if(CLANG_TIDY_EXE)
        set(CMAKE_CXX_CLANG_TIDY "${CLANG_TIDY_EXE}")
        message(STATUS "clang-tidy: ${CLANG_TIDY_EXE}")
    else()
        message(WARNING "ENABLE_CLANG_TIDY=ON but clang-tidy not found in PATH")
    endif()
endif()

# Modem / FSK layer
add_library(ale_fsk_core
    src/FSK/tone_generator.cpp
)

target_include_directories(ale_fsk_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# FEC layer
add_library(ale_fec
    src/FEC/golay.cpp
    src/FEC/word_interleaver.cpp
    src/FEC/ale_fec_codec.cpp
)

target_include_directories(ale_fec PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Protocol / Word layer
add_library(ale_protocol
    src/Word/ale_word.cpp
    src/Word/ale_sequence.cpp
    src/Word/address_encoder.cpp
    src/Protocol/Message/ale_message.cpp
    src/Protocol/Message/frame_validator.cpp
    src/Protocol/Message/ale_frame_builder.cpp
    src/Protocol/Message/ale_orderwire_protocols.cpp
    src/Protocol/Message/ale_gpr.cpp
    src/LQA/lqa_report.cpp
)

target_include_directories(ale_protocol PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(ale_protocol ale_fsk_core ale_fec)

# AQC protocol extension
add_library(ale_aqc
    src/Protocol/AQC/aqc_parser.cpp
)

target_include_directories(ale_aqc PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(ale_aqc ale_protocol ale_fsk_core ale_fec)

# Version/Capabilities protocol (FEAT-GEN-011, FEAT-GEN-012)
add_library(ale_version_caps
    src/Protocol/ale_version_caps.cpp
)

target_include_directories(ale_version_caps PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(ale_version_caps ale_protocol ale_fsk_core ale_fec)

# ALE 2G Codec — word ↔ symbol-frame pipeline (encoder + decoder)
add_library(ale_codec
    src/Codec/ale_encoder.cpp
    src/Codec/ale_decoder.cpp
)

target_include_directories(ale_codec PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(ale_codec ale_fec ale_protocol ale_fsk_core)

# ALE 2G Modem — word-level TX with 3× redundancy (A.5.2.2.4)
add_library(ale_modem
    src/Modem/ale2g_modem.cpp        # Modulator + Demodulator (signal extraction)
    src/Modem/word_grid_tracker.cpp  # A.5.2.6.3 grid-lock state machine
    src/Modem/spectrum_analyser.cpp  # Waterfall / spectrum analyser (display)
)

target_include_directories(ale_modem PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(ale_modem ale_codec ale_protocol ale_fsk_core ale_fec ale_pal)

# ALE state machine (link layer)
add_library(ale_link
    src/Protocol/Control/ale_state_machine.cpp
    src/Protocol/Control/ale_call_processor.cpp
    src/Protocol/Control/ale_channel_manager.cpp
    src/Protocol/Control/ale_freq_select.cpp
)

target_include_directories(ale_link PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(ale_link ale_stores ale_protocol ale_fsk_core ale_fec ale_pal)

# FS-1052 ARQ
add_library(ale_fs1052
    src/FS1052/frame_format.cpp
    src/FS1052/fs1052_arq.cpp
)

target_include_directories(ale_fs1052 PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(ale_fs1052 ale_protocol ale_fsk_core ale_fec)

# LQA system
add_library(ale_lqa
    src/LQA/lqa_database.cpp
    src/LQA/lqa_history.cpp
    src/LQA/lqa_metrics.cpp
    src/LQA/lqa_analyzer.cpp
    src/LQA/lqa_exchange.cpp
    src/LQA/solar_position.cpp
)

target_include_directories(ale_lqa PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(ale_lqa ale_protocol ale_fsk_core ale_fec)

# Data stores [geplant]
add_library(ale_stores
    src/Stores/ale_data_store.cpp
    src/Stores/address_book.cpp
)

target_include_directories(ale_stores PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(ale_stores ale_lqa ale_protocol ale_fsk_core ale_fec)

# Channel selection [geplant]
add_library(ale_channel
    src/Protocol/Control/ale_channel_selector.cpp
)

target_include_directories(ale_channel PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(ale_channel ale_stores ale_lqa ale_protocol ale_fsk_core ale_fec)

# ── PAL — platform abstraction implementations ────────────────────────────────

# ── Hamlib (Radio CAT + PTT) ──────────────────────────────────────────────────
#
# Hamlib hat kein CMakeLists.txt — es wird einmalig per Skript aus dem
# Quellcode gebaut und als statische Library eingebunden.
#
# Einmalig in einer MSYS2 MinGW64-Shell:
#   bash scripts/build_hamlib.sh
#
# Danach erkennt CMake die installierte Lib automatisch.
# Hamlib ist verpflichtend: fehlt es, schlägt das CMake-Configure mit
# FATAL_ERROR fehl (ein Build ohne Hamlib linkt eine Binary, die beim ersten
# GUI-Connect segfaultet). HAMLIB_BUILD=OFF wird nicht unterstützt.

option(HAMLIB_BUILD "Hamlib-Support einbauen (Radio CAT/PTT)" ON)

set(HAMLIB_FOUND FALSE)
set(_hamlib_inc "")
set(_hamlib_lib "")

if(HAMLIB_BUILD)
    # 1. Bevorzuge den selbst gebauten Build aus scripts/build_hamlib.sh
    set(_hamlib_prefix "${CMAKE_CURRENT_SOURCE_DIR}/libs/hamlib-built")
    if(EXISTS "${_hamlib_prefix}/include/hamlib/rig.h")
        if(MSVC AND EXISTS "${_hamlib_prefix}/lib/libhamlib.lib")
            # MSVC: Import-Library (.lib) + DLL
            set(HAMLIB_FOUND TRUE)
            set(_hamlib_lib "${_hamlib_prefix}/lib/libhamlib.lib")
            set(_hamlib_bin "${_hamlib_prefix}/bin")
            message(STATUS "Hamlib: DLL-Build gefunden, MSVC Import-Library (libs/hamlib-built/)")
        elseif(NOT MSVC AND EXISTS "${_hamlib_prefix}/lib/libhamlib.dll.a")
            # MinGW: MinGW Import-Library (.dll.a)
            set(HAMLIB_FOUND TRUE)
            set(_hamlib_lib "${_hamlib_prefix}/lib/libhamlib.dll.a")
            set(_hamlib_bin "${_hamlib_prefix}/bin")
            message(STATUS "Hamlib: DLL-Build gefunden, MinGW Import-Library (libs/hamlib-built/)")
        elseif(EXISTS "${_hamlib_prefix}/lib/libhamlib.a")
            # Fallback: statische Library (alter Build ohne --enable-shared)
            set(HAMLIB_FOUND TRUE)
            set(_hamlib_lib "${_hamlib_prefix}/lib/libhamlib.a")
            message(STATUS "Hamlib: statischer Build gefunden (libs/hamlib-built/)")
        endif()
        if(HAMLIB_FOUND)
            set(_hamlib_inc "${_hamlib_prefix}/include")
        endif()
    endif()

    # 2. Fallback: pkg-config (Linux / MSYS2 system-install)
    if(NOT HAMLIB_FOUND)
        find_package(PkgConfig QUIET)
        if(PkgConfig_FOUND)
            pkg_check_modules(HAMLIB QUIET hamlib)
            if(HAMLIB_FOUND)
                set(_hamlib_inc "${HAMLIB_INCLUDE_DIRS}")
                set(_hamlib_lib "${HAMLIB_LIBRARIES}")
                message(STATUS "Hamlib: via pkg-config gefunden")
            endif()
        endif()
    endif()

endif()

# Hamlib is mandatory: the bridge (apps/ale_bridge.cpp) unconditionally calls
# pal::list_rigs() / pal::rig_port_type() / pal::hamlib_set_log_level(), which
# are only defined when hamlib_radio.cpp is compiled (HAVE_HAMLIB). A "found
# headers but missing runtime backends" build links but segfaults on the first
# WS client connect (RIG_LIST barrage -> rig_load_all_backends). Fail configure
# here whenever Hamlib was not detected -- including HAMLIB_BUILD=OFF, which
# skips the search and would otherwise link a broken binary.
if(NOT HAMLIB_FOUND)
    message(FATAL_ERROR
        "Hamlib NOT found. openALE hard-requires Hamlib -- a build without it "
        "links a binary that segfaults on the first GUI connect. Install Hamlib "
        "and re-run cmake:\n"
        "  Linux:   sudo apt install libhamlib-dev   (then pkg-config finds it)\n"
        "           or build from source: bash scripts/build_hamlib.sh\n"
        "  Windows: bash scripts/build_hamlib.sh  (MSYS2 MinGW64) -> libs/hamlib-built/\n"
        "HAMLIB_BUILD=OFF (no-radio build) is not currently supported.")
endif()

# ── mbedTLS (native TLS for the GUI bridge's HTTPS/WSS support) ───────────────
#
# Vendored via FetchContent, pinned to the 3.6.x LTS branch. Unlike Hamlib
# above, mbedTLS ships its own CMake build, so it builds identically on
# Windows (MSVC) and Linux from the same source — no prebuilt-binary
# vendoring step required. ENABLE_PROGRAMS/ENABLE_TESTING are forced off so
# only the three libraries (mbedtls, mbedx509, mbedcrypto) get built, not
# mbedTLS's own sample programs/test suite — but its build still needs the
# "framework" submodule at configure time (scripts/config.py imports from
# it), so submodules must be fetched even though tests aren't built.
include(FetchContent)
set(ENABLE_PROGRAMS OFF CACHE BOOL "" FORCE)
set(ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(MBEDTLS_FATAL_WARNINGS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    mbedtls
    GIT_REPOSITORY https://github.com/Mbed-TLS/mbedtls.git
    GIT_TAG        mbedtls-3.6.7
    GIT_SHALLOW    TRUE
    GIT_SUBMODULES_RECURSE TRUE
)
FetchContent_MakeAvailable(mbedtls)

# ── Ed25519 (Location Relay per-callsign signing) ─────────────────────────────
#
# mbedTLS at the pinned mbedtls-3.6.7 has no usable classic-API (mbedtls_pk)
# Ed25519 support, so Location Relay's per-station signing vendors a small,
# dependency-free implementation instead of adding libsodium as a whole new
# external dependency. See third_party/ed25519/VERSION.md for provenance.
add_library(ale_ed25519 STATIC
    third_party/ed25519/keypair.c
    third_party/ed25519/sign.c
    third_party/ed25519/verify.c
    third_party/ed25519/seed.c
    third_party/ed25519/sha512.c
    third_party/ed25519/fe.c
    third_party/ed25519/ge.c
    third_party/ed25519/sc.c
)
target_include_directories(ale_ed25519 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/third_party/ed25519)

add_library(ale_pal
    src/PAL/logger.cpp
    src/PAL/crash_handler.cpp
    src/PAL/events.cpp
    src/PAL/timer.cpp
    src/PAL/radio.cpp
    src/PAL/paths.cpp
)

target_include_directories(ale_pal PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

if(WIN32)
    target_link_libraries(ale_pal winmm dbghelp)
endif()

if(HAMLIB_FOUND)
    target_sources(ale_pal PRIVATE src/PAL/radios/hamlib_radio.cpp)
    target_link_libraries(ale_pal "${_hamlib_lib}")
    target_compile_definitions(ale_pal PUBLIC HAVE_HAMLIB)

    if(MSVC)
        # Hamlib's rig.h inkludiert <pthread.h> — MSVC hat das nicht.
        # MinGW winpthreads definiert alle pthread-Typen als Pointer (8 Byte auf 64-Bit).
        # Dieser Stub gibt MSVC die nötigen Typdeklarationen ohne MinGW-Abhängigkeiten.
        # Stub wird VOR Hamlib-Include gesetzt, damit <pthread.h> hier zuerst gefunden wird.
        set(_pthread_compat_dir "${CMAKE_BINARY_DIR}/hamlib_msvc_compat")
        file(MAKE_DIRECTORY "${_pthread_compat_dir}")
        file(WRITE "${_pthread_compat_dir}/pthread.h"
"/* Auto-generated MSVC stub — pthread types as in MinGW winpthreads (pointer-sized) */\n\
#pragma once\n\
#ifndef HAMLIB_PTHREAD_STUB_H\n\
#define HAMLIB_PTHREAD_STUB_H\n\
typedef void* pthread_t;\n\
typedef void* pthread_mutex_t;\n\
typedef void* pthread_cond_t;\n\
typedef void* pthread_rwlock_t;\n\
typedef int   pthread_mutexattr_t;\n\
typedef int   pthread_condattr_t;\n\
typedef int   pthread_rwlockattr_t;\n\
typedef int   pthread_attr_t;\n\
typedef int   pthread_key_t;\n\
typedef int   pthread_once_t;\n\
#define PTHREAD_MUTEX_INITIALIZER ((pthread_mutex_t)0)\n\
#define PTHREAD_COND_INITIALIZER  ((pthread_cond_t)0)\n\
#define PTHREAD_RWLOCK_INITIALIZER ((pthread_rwlock_t)0)\n\
#define PTHREAD_ONCE_INIT 0\n\
#endif\n")
        file(WRITE "${_pthread_compat_dir}/sched.h"
"#pragma once\n\
#ifndef HAMLIB_SCHED_STUB_H\n\
#define HAMLIB_SCHED_STUB_H\n\
#define SCHED_OTHER 0\n\
#define SCHED_FIFO  1\n\
#define SCHED_RR    2\n\
struct sched_param { int sched_priority; };\n\
#endif\n")
        file(WRITE "${_pthread_compat_dir}/semaphore.h"
"#pragma once\n\
#ifndef HAMLIB_SEMAPHORE_STUB_H\n\
#define HAMLIB_SEMAPHORE_STUB_H\n\
typedef void* sem_t;\n\
#endif\n")
        # Compat-Dir VOR Hamlib-Include — Compiler findet pthread.h hier zuerst
        target_include_directories(ale_pal PRIVATE
            "${_pthread_compat_dir}"
            "${_hamlib_inc}"
        )
    else()
        target_include_directories(ale_pal PRIVATE "${_hamlib_inc}")
    endif()
endif()

# ── Tests ─────────────────────────────────────────────────────────────────────

# ── App layer (ALEController + ALERxPipeline) ─────────────────────────────────

add_library(ale_app_core
    src/App/ale_controller.cpp
    src/App/audio_monitor.cpp
    src/App/audio_transport.cpp
    src/App/freq_select_manager.cpp
    src/App/gps_service.cpp
    src/App/http_poster.cpp
    src/App/location_relay_service.cpp
    src/App/relay_identity.cpp
    src/App/sfi_service.cpp
    src/App/sounding_identity_accumulator.cpp
    src/App/voice_path_manager.cpp
)

target_include_directories(ale_app_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(ale_app_core
    ale_link ale_modem ale_protocol ale_fsk_core ale_fec ale_pal
)

if(WIN32)
    target_link_libraries(ale_app_core ws2_32)
    if(NOT MSVC)
        target_link_libraries(ale_app_core winhttp)
    endif()
endif()

# http_poster.cpp's POSIX backend uses mbedTLS directly for its https:// client
# (see the mbedTLS FetchContent block below for why it's already a project
# dependency) — makes the headers visible to ale_app_core's compile step; the
# openALE executable target already links these three libs explicitly for the
# bridge's own TLS support.
target_link_libraries(ale_app_core mbedtls mbedx509 mbedcrypto)
target_link_libraries(ale_app_core ale_ed25519)

# ── Audio I/O device (platform-specific) ──────────────────────────────────────

if(WIN32)
    set(AUDIO_DEVICE_SRC src/App/wasapi_audio.cpp)
elseif(UNIX AND NOT APPLE)
    set(AUDIO_DEVICE_SRC src/App/alsa_audio.cpp)
else()
    set(AUDIO_DEVICE_SRC src/App/null_audio.cpp)
endif()

add_library(ale_audio_device ${AUDIO_DEVICE_SRC})

target_include_directories(ale_audio_device PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

if(WIN32)
    # WASAPI (mmdeviceapi/audioclient) + COM device enumeration.
    # ole32: COM; uuid: PKEY_Device_FriendlyName property key.
    target_link_libraries(ale_audio_device ole32 uuid)
elseif(UNIX AND NOT APPLE)
    target_link_libraries(ale_audio_device asound)
endif()

# ── Mock Radio Server (rigctld-kompatibel, kein Hamlib nötig) ─────────────────

add_executable(radio_mock apps/radio_mock.cpp)

target_include_directories(radio_mock PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

if(WIN32)
    target_link_libraries(radio_mock ws2_32)
endif()

# ── Hamlib protocol smoke-test (connects to radio_mock, verifies command format) ──
if(HAMLIB_FOUND)
    add_executable(test_hamlib_mock apps/test_hamlib_mock.cpp)
    if(MSVC)
        # pthread compat stub must precede hamlib includes (same as ale_pal)
        target_include_directories(test_hamlib_mock PRIVATE
            "${_pthread_compat_dir}"
            "${_hamlib_inc}"
            ${CMAKE_CURRENT_SOURCE_DIR}/include
        )
    else()
        target_include_directories(test_hamlib_mock PRIVATE
            "${_hamlib_inc}"
            ${CMAKE_CURRENT_SOURCE_DIR}/include
        )
    endif()
    target_link_libraries(test_hamlib_mock "${_hamlib_lib}")
    if(WIN32)
        target_link_libraries(test_hamlib_mock ws2_32)
    endif()
endif()

# ── Relay identity (Ed25519 signing) smoke tests ──────────────────────────────

# Standalone: sign/verify round-trip, tampered-body rejection, key-file
# round-trip, deterministic keypair-from-seed known-answer test. No sockets,
# no ALEController, run manually (matches the project's existing
# add_executable smoke-test pattern — no CTest/enable_testing()).
add_executable(test_relay_identity apps/test_relay_identity.cpp src/App/relay_identity.cpp src/PAL/logger.cpp)
target_include_directories(test_relay_identity PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(test_relay_identity ale_ed25519)
if(WIN32)
    target_link_libraries(test_relay_identity bcrypt)
endif()

# ── rigctld-compat protocol smoke tests ────────────────────────────────────────

# Pure protocol function — no sockets, no Hamlib, no ALEController.
add_executable(test_rigctld_protocol apps/test_rigctld_protocol.cpp)
target_include_directories(test_rigctld_protocol PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/apps
)

# Real Hamlib RIG_MODEL_NETRIGCTL client against an in-process RigctldServer —
# the \dump_state open()-handshake and write-rejection regression guard.
if(HAMLIB_FOUND)
    add_executable(test_rigctld_hamlib apps/test_rigctld_hamlib.cpp apps/bridge/rigctld_server.cpp src/PAL/logger.cpp)
    if(MSVC)
        target_include_directories(test_rigctld_hamlib PRIVATE
            "${_pthread_compat_dir}"
            "${_hamlib_inc}"
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${CMAKE_CURRENT_SOURCE_DIR}/apps
        )
    else()
        target_include_directories(test_rigctld_hamlib PRIVATE
            "${_hamlib_inc}"
            ${CMAKE_CURRENT_SOURCE_DIR}/include
            ${CMAKE_CURRENT_SOURCE_DIR}/apps
        )
    endif()
    target_link_libraries(test_rigctld_hamlib "${_hamlib_lib}")
    if(WIN32)
        target_link_libraries(test_rigctld_hamlib ws2_32)
    endif()
endif()

# ── WebSocket bridge (GUI↔Core) ────────────────────────────────────────────────

add_executable(openALE apps/ale_bridge.cpp apps/bridge/ws_server.cpp apps/bridge/rigctld_server.cpp apps/bridge/tls_support.cpp)

target_include_directories(openALE PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/apps
)

target_link_libraries(openALE ale_app_core ale_audio_device ale_stores ale_pal mbedtls mbedx509 mbedcrypto)

if(WIN32)
    target_link_libraries(openALE ws2_32 iphlpapi)
else()
    # Export symbols to the dynamic symbol table so backtrace_symbols() in
    # src/PAL/crash_handler.cpp can actually name in-executable frames instead
    # of just "openALE(+0x1234)".
    target_link_options(openALE PRIVATE -rdynamic)
endif()

# DLL-Build: libhamlib-4.dll + Runtime-DLLs neben openALE.exe kopieren
# (muss nach add_executable stehen)
if(HAMLIB_FOUND AND DEFINED _hamlib_bin)
    file(GLOB _hamlib_dlls "${_hamlib_bin}/*.dll")
    foreach(_dll ${_hamlib_dlls})
        add_custom_command(TARGET openALE POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${_dll}" "$<TARGET_FILE_DIR:openALE>"
        )
    endforeach()
endif()

# ─────────────────────────────────────────────────────────────────────────────

enable_testing()

add_executable(test_fsk_core tests/waveform/unit/test_fsk_core.cpp)
target_link_libraries(test_fsk_core ale_fsk_core ale_fec)
target_include_directories(test_fsk_core PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
add_test(NAME FSKCore COMMAND test_fsk_core)

add_executable(test_tone_accuracy tests/waveform/unit/test_tone_accuracy.cpp)
target_link_libraries(test_tone_accuracy ale_fsk_core ale_fec)
target_include_directories(test_tone_accuracy PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
add_test(NAME ToneAccuracy COMMAND test_tone_accuracy)

add_executable(test_tone_thd tests/waveform/unit/test_tone_thd.cpp)
target_link_libraries(test_tone_thd ale_fsk_core ale_fec)
target_include_directories(test_tone_thd PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
add_test(NAME ToneTHD COMMAND test_tone_thd)

add_executable(test_tx_bandpass tests/waveform/unit/test_tx_bandpass.cpp)
target_link_libraries(test_tx_bandpass ale_fsk_core ale_fec)
target_include_directories(test_tx_bandpass PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
add_test(NAME TxBandpass COMMAND test_tx_bandpass)

add_executable(test_fec_golay tests/fec/unit/test_fec_golay.cpp)
target_link_libraries(test_fec_golay ale_codec ale_fec ale_fsk_core)
target_include_directories(test_fec_golay PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
add_test(NAME FECGolay COMMAND test_fec_golay)

add_executable(test_protocol tests/word/unit/test_protocol.cpp)
target_link_libraries(test_protocol ale_stores ale_protocol ale_fsk_core ale_fec)
target_include_directories(test_protocol PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
add_test(NAME Protocol COMMAND test_protocol)

add_executable(test_state_machine tests/link/unit/test_state_machine.cpp)
target_link_libraries(test_state_machine ale_link ale_protocol ale_fsk_core ale_fec)
target_include_directories(test_state_machine PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
add_test(NAME StateMachine COMMAND test_state_machine)

add_executable(test_foreign_twas_link_guard tests/link/unit/test_foreign_twas_link_guard.cpp)
target_link_libraries(test_foreign_twas_link_guard ale_link ale_protocol ale_fsk_core ale_fec)
target_include_directories(test_foreign_twas_link_guard PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
add_test(NAME ForeignTwasLinkGuard COMMAND test_foreign_twas_link_guard)

add_executable(test_rx_characterization tests/link/unit/test_rx_characterization.cpp)
target_link_libraries(test_rx_characterization ale_link ale_protocol ale_fsk_core ale_fec)
target_include_directories(test_rx_characterization PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
add_test(NAME RxCharacterization COMMAND test_rx_characterization)

add_executable(test_aqc_word_format tests/aqc/unit/test_aqc_word_format.cpp)
target_link_libraries(test_aqc_word_format ale_aqc ale_protocol ale_fsk_core ale_fec)
target_include_directories(test_aqc_word_format PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
add_test(NAME AQCWordFormat COMMAND test_aqc_word_format)

add_executable(test_aqc_parser tests/aqc/unit/test_aqc_parser.cpp)
target_link_libraries(test_aqc_parser ale_aqc ale_protocol ale_fsk_core ale_fec)
target_include_directories(test_aqc_parser PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
add_test(NAME AQCParser COMMAND test_aqc_parser)

add_executable(test_aqc_crc tests/aqc/unit/test_aqc_crc.cpp)
target_link_libraries(test_aqc_crc ale_aqc ale_protocol ale_fsk_core ale_fec)
target_include_directories(test_aqc_crc PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
add_test(NAME AQCCRC COMMAND test_aqc_crc)

add_executable(test_aqc_backward_compat tests/aqc/integration/test_aqc_backward_compat.cpp)
target_link_libraries(test_aqc_backward_compat ale_aqc ale_protocol ale_fsk_core ale_fec)
target_include_directories(test_aqc_backward_compat PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
add_test(NAME AQCBackwardCompat COMMAND test_aqc_backward_compat)

add_executable(test_aqc_fixed_bit tests/aqc/unit/test_aqc_fixed_bit.cpp)
target_link_libraries(test_aqc_fixed_bit ale_aqc ale_protocol ale_fsk_core ale_fec)
target_include_directories(test_aqc_fixed_bit PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
add_test(NAME AQCFixedBit COMMAND test_aqc_fixed_bit)

add_executable(test_version_caps tests/gen/unit/test_version_caps.cpp)
target_link_libraries(test_version_caps ale_version_caps ale_protocol ale_fsk_core ale_fec)
target_include_directories(test_version_caps PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
add_test(NAME VersionCaps COMMAND test_version_caps)

add_executable(test_fs1052_frames tests/gen/unit/test_fs1052_frames.cpp)
target_link_libraries(test_fs1052_frames ale_fs1052 ale_protocol ale_fsk_core ale_fec)
target_include_directories(test_fs1052_frames PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
add_test(NAME FS1052Frames COMMAND test_fs1052_frames)

add_executable(test_fs1052_arq tests/gen/unit/test_fs1052_arq.cpp)
target_link_libraries(test_fs1052_arq ale_fs1052 ale_protocol ale_fsk_core ale_fec)
target_include_directories(test_fs1052_arq PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
add_test(NAME FS1052ARQ COMMAND test_fs1052_arq)

add_executable(test_amd_callback tests/gen/unit/test_amd_callback.cpp)
target_link_libraries(test_amd_callback ale_protocol ale_fsk_core ale_fec)
target_include_directories(test_amd_callback PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
add_test(NAME AMDCallback COMMAND test_amd_callback)

add_executable(test_amd_orderwire_tx tests/gen/unit/test_amd_orderwire_tx.cpp)
target_link_libraries(test_amd_orderwire_tx ale_link ale_protocol ale_fsk_core ale_fec)
target_include_directories(test_amd_orderwire_tx PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
add_test(NAME AMDOrderwireTx COMMAND test_amd_orderwire_tx)

add_executable(test_amd_linked_orderwire tests/gen/unit/test_amd_linked_orderwire.cpp)
target_link_libraries(test_amd_linked_orderwire ale_link ale_protocol ale_fsk_core ale_fec)
target_include_directories(test_amd_linked_orderwire PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
add_test(NAME AMDLinkedOrderwire COMMAND test_amd_linked_orderwire)

add_executable(test_amd_send tests/gen/unit/test_amd_send.cpp)
target_link_libraries(test_amd_send ale_app_core ale_link ale_modem ale_protocol ale_fsk_core ale_fec)
target_include_directories(test_amd_send PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
add_test(NAME AMDSend COMMAND test_amd_send)

add_executable(test_amd_calling_frame tests/gen/unit/test_amd_calling_frame.cpp)
target_link_libraries(test_amd_calling_frame ale_app_core ale_link ale_modem ale_protocol ale_fsk_core ale_fec ale_pal)
target_include_directories(test_amd_calling_frame PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
add_test(NAME AMDCallingFrame COMMAND test_amd_calling_frame)

add_executable(test_dtm_orderwire_tx tests/gen/unit/test_dtm_orderwire_tx.cpp)
target_link_libraries(test_dtm_orderwire_tx ale_protocol ale_fsk_core ale_fec)
target_include_directories(test_dtm_orderwire_tx PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
add_test(NAME DTMOrderwireTx COMMAND test_dtm_orderwire_tx)

add_executable(test_dbm_orderwire_tx tests/gen/unit/test_dbm_orderwire_tx.cpp)
target_link_libraries(test_dbm_orderwire_tx ale_protocol ale_fsk_core ale_fec)
target_include_directories(test_dbm_orderwire_tx PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
add_test(NAME DBMOrderwireTx COMMAND test_dbm_orderwire_tx)

add_executable(test_lqa_database tests/gen/unit/test_lqa_database.cpp)
target_link_libraries(test_lqa_database ale_lqa ale_protocol ale_fsk_core ale_fec)
target_include_directories(test_lqa_database PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
add_test(NAME LQADatabase COMMAND test_lqa_database)

add_executable(test_lqa_metrics tests/chan/unit/test_lqa_metrics.cpp)
target_link_libraries(test_lqa_metrics ale_lqa ale_protocol ale_fsk_core ale_fec)
target_include_directories(test_lqa_metrics PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
add_test(NAME LQAMetrics COMMAND test_lqa_metrics)

add_executable(test_lqa_analyzer tests/chan/unit/test_lqa_analyzer.cpp)
target_link_libraries(test_lqa_analyzer ale_lqa ale_protocol ale_fsk_core ale_fec)
target_include_directories(test_lqa_analyzer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
add_test(NAME LQAAnalyzer COMMAND test_lqa_analyzer)

add_executable(test_lqa_report tests/lqa/unit/test_lqa_report.cpp)
target_link_libraries(test_lqa_report ale_lqa ale_protocol ale_fsk_core ale_fec)
target_include_directories(test_lqa_report PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
add_test(NAME LQAReport COMMAND test_lqa_report)

add_executable(test_lqa_noise_cmd tests/chan/unit/test_lqa_noise_cmd.cpp)
target_link_libraries(test_lqa_noise_cmd ale_lqa ale_protocol ale_fsk_core ale_fec)
target_include_directories(test_lqa_noise_cmd PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
add_test(NAME LQANoiseCmd COMMAND test_lqa_noise_cmd)

add_executable(test_lbt_occupancy tests/chan/unit/test_lbt_occupancy.cpp)
target_link_libraries(test_lbt_occupancy ale_app_core ale_lqa ale_link ale_modem ale_protocol ale_fsk_core ale_fec)
target_include_directories(test_lbt_occupancy PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
add_test(NAME LbtOccupancy COMMAND test_lbt_occupancy)

add_executable(test_lqa_purity tests/chan/unit/test_lqa_purity.cpp)
target_link_libraries(test_lqa_purity ale_app_core ale_lqa ale_link ale_modem ale_protocol ale_fsk_core ale_fec)
target_include_directories(test_lqa_purity PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
add_test(NAME LQAPurity COMMAND test_lqa_purity)

add_executable(test_voice_path_manager tests/chan/unit/test_voice_path_manager.cpp)
target_link_libraries(test_voice_path_manager ale_app_core)
target_include_directories(test_voice_path_manager PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
add_test(NAME VoicePathManager COMMAND test_voice_path_manager)

add_executable(test_audio_transport tests/chan/unit/test_audio_transport.cpp)
target_link_libraries(test_audio_transport ale_app_core)
target_include_directories(test_audio_transport PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
add_test(NAME AudioTransport COMMAND test_audio_transport)

add_executable(test_lqa_bilateral_tx tests/chan/unit/test_lqa_bilateral_tx.cpp)
target_link_libraries(test_lqa_bilateral_tx ale_app_core ale_lqa ale_link ale_modem ale_protocol ale_fsk_core ale_fec)
target_include_directories(test_lqa_bilateral_tx PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
add_test(NAME LQABilateralTx COMMAND test_lqa_bilateral_tx)

add_executable(test_lqa_bilateral_rx tests/chan/unit/test_lqa_bilateral_rx.cpp)
target_link_libraries(test_lqa_bilateral_rx ale_app_core ale_lqa ale_link ale_modem ale_protocol ale_fsk_core ale_fec)
target_include_directories(test_lqa_bilateral_rx PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
add_test(NAME LQABilateralRx COMMAND test_lqa_bilateral_rx)

add_executable(test_bilateral_lqa tests/link/integration/test_bilateral_lqa.cpp)
target_link_libraries(test_bilateral_lqa ale_app_core ale_lqa ale_link ale_modem ale_protocol ale_fsk_core ale_fec)
target_include_directories(test_bilateral_lqa PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
add_test(NAME BilateralLQA COMMAND test_bilateral_lqa)

add_executable(test_lqa_report_bounds tests/chan/unit/test_lqa_report_bounds.cpp)
target_link_libraries(test_lqa_report_bounds ale_app_core ale_lqa ale_link ale_modem ale_protocol ale_fsk_core ale_fec)
target_include_directories(test_lqa_report_bounds PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
add_test(NAME LQAReportBounds COMMAND test_lqa_report_bounds)

add_executable(test_address_encoder tests/addr/unit/test_address_encoder.cpp)
target_link_libraries(test_address_encoder ale_protocol ale_fsk_core ale_fec)
target_include_directories(test_address_encoder PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
add_test(NAME AddressEncoder COMMAND test_address_encoder)

add_executable(test_addr_validation tests/addr/unit/test_addr_validation.cpp)
target_link_libraries(test_addr_validation ale_app_core ale_link ale_modem ale_protocol ale_fsk_core ale_fec)
target_include_directories(test_addr_validation PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
add_test(NAME AddrValidation COMMAND test_addr_validation)

add_executable(test_ale_calling tests/word/unit/test_ale_calling.cpp)
target_link_libraries(test_ale_calling ale_link ale_protocol ale_fsk_core ale_fec)
target_include_directories(test_ale_calling PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
add_test(NAME ALECalling COMMAND test_ale_calling)

add_executable(test_frame_builder tests/word/unit/test_frame_builder.cpp)
target_link_libraries(test_frame_builder ale_protocol ale_fsk_core ale_fec)
target_include_directories(test_frame_builder PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
add_test(NAME FrameBuilder COMMAND test_frame_builder)

add_executable(test_frame tests/frame/unit/test_frame.cpp)
target_link_libraries(test_frame ale_link ale_modem ale_protocol ale_fsk_core ale_fec)
target_include_directories(test_frame PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
add_test(NAME Frame COMMAND test_frame)

add_executable(test_frame_c tests/frame/integration/test_frame_c.cpp)
target_link_libraries(test_frame_c ale_link ale_modem ale_protocol ale_fsk_core ale_fec)
target_include_directories(test_frame_c PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
add_test(NAME FrameC COMMAND test_frame_c)

add_executable(test_resampler_path tests/waveform/integration/test_resampler_path.cpp)
target_link_libraries(test_resampler_path ale_app_core ale_link ale_modem ale_protocol ale_fsk_core ale_fec)
target_include_directories(test_resampler_path PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
add_test(NAME ResamplerPath COMMAND test_resampler_path)

add_executable(test_channel_net_store tests/chan/unit/test_channel_net_store.cpp)
target_link_libraries(test_channel_net_store ale_app_core ale_link ale_modem ale_protocol ale_fsk_core ale_fec)
target_include_directories(test_channel_net_store PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
add_test(NAME ChannelNetStore COMMAND test_channel_net_store)

add_executable(test_test_channel tests/chan/unit/test_test_channel.cpp)
target_link_libraries(test_test_channel ale_app_core ale_link ale_modem ale_protocol ale_fsk_core ale_fec)
target_include_directories(test_test_channel PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
add_test(NAME TestChannel COMMAND test_test_channel)

add_executable(test_sounding_timing tests/chan/unit/test_sounding_timing.cpp)
target_link_libraries(test_sounding_timing ale_app_core ale_link ale_modem ale_protocol ale_fsk_core ale_fec)
target_include_directories(test_sounding_timing PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
add_test(NAME SoundingTiming COMMAND test_sounding_timing)

add_executable(test_sounding_identity_accumulator tests/chan/unit/test_sounding_identity_accumulator.cpp)
target_link_libraries(test_sounding_identity_accumulator ale_app_core ale_link ale_modem ale_protocol ale_fsk_core ale_fec)
target_include_directories(test_sounding_identity_accumulator PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
add_test(NAME SoundingIdentityAccumulator COMMAND test_sounding_identity_accumulator)

add_executable(test_channel_selector tests/chan/unit/test_channel_selector.cpp)
target_link_libraries(test_channel_selector ale_channel ale_stores ale_lqa ale_protocol ale_fsk_core ale_fec)
target_include_directories(test_channel_selector PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
add_test(NAME ChannelSelector COMMAND test_channel_selector)

add_executable(test_ws_bridge_utils tests/gen/unit/test_ws_bridge_utils.cpp)
target_include_directories(test_ws_bridge_utils PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/apps)
add_test(NAME WsBridgeUtils COMMAND test_ws_bridge_utils)

add_executable(test_roundtrip tests/waveform/e2e/test_roundtrip.cpp)
target_link_libraries(test_roundtrip ale_link ale_modem ale_protocol ale_fsk_core ale_fec)
target_include_directories(test_roundtrip PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
add_test(NAME Roundtrip COMMAND test_roundtrip)

add_executable(test_codec tests/gen/integration/test_codec.cpp)
target_link_libraries(test_codec ale_codec ale_protocol ale_fsk_core ale_fec)
target_include_directories(test_codec PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
add_test(NAME Codec COMMAND test_codec)

add_executable(test_pcale_interop tests/gen/integration/test_pcale_interop.cpp)
target_link_libraries(test_pcale_interop ale_codec ale_protocol ale_fsk_core ale_fec)
target_include_directories(test_pcale_interop PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
add_test(NAME PCALEInterop COMMAND test_pcale_interop)

add_executable(test_sync tests/sync/unit/test_sync.cpp)
target_link_libraries(test_sync ale_link ale_protocol ale_fsk_core ale_fec)
target_include_directories(test_sync PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
add_test(NAME Sync COMMAND test_sync)

add_executable(test_decoder_robustness tests/sync/integration/test_decoder_robustness.cpp)
target_link_libraries(test_decoder_robustness ale_modem ale_codec ale_protocol ale_fsk_core ale_fec)
target_include_directories(test_decoder_robustness PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
add_test(NAME DecoderRobustness COMMAND test_decoder_robustness)

add_executable(test_sync_criteria tests/sync/unit/test_sync_criteria.cpp)
target_link_libraries(test_sync_criteria ale_modem ale_codec ale_protocol ale_fsk_core ale_fec)
target_include_directories(test_sync_criteria PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
add_test(NAME SyncCriteria COMMAND test_sync_criteria)

add_executable(test_word_grid_tracker tests/sync/unit/test_word_grid_tracker.cpp)
target_link_libraries(test_word_grid_tracker ale_modem ale_codec ale_protocol ale_fsk_core ale_fec)
target_include_directories(test_word_grid_tracker PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
add_test(NAME WordGridTracker COMMAND test_word_grid_tracker)

add_executable(test_stage1_detector tests/sync/unit/test_stage1_detector.cpp)
target_link_libraries(test_stage1_detector ale_modem ale_codec ale_protocol ale_fsk_core ale_fec)
target_include_directories(test_stage1_detector PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
add_test(NAME Stage1Detector COMMAND test_stage1_detector)

add_executable(test_word_sync tests/sync/integration/test_word_sync.cpp)
target_link_libraries(test_word_sync ale_modem ale_codec ale_protocol ale_fsk_core ale_fec)
target_include_directories(test_word_sync PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
add_test(NAME WordSync COMMAND test_word_sync)

add_executable(test_sinad_rework tests/waveform/e2e/test_sinad_rework.cpp)
target_link_libraries(test_sinad_rework ale_modem ale_codec ale_protocol ale_fsk_core ale_fec)
target_include_directories(test_sinad_rework PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
add_test(NAME SinadRework COMMAND test_sinad_rework)

add_executable(test_architecture tests/gen/integration/test_layering.cpp)
target_compile_definitions(test_architecture PRIVATE PROJECT_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME Architecture COMMAND test_architecture)

add_executable(test_message_assembler tests/monitor/unit/test_message_assembler.cpp)
target_link_libraries(test_message_assembler ale_protocol ale_fsk_core ale_fec)
target_include_directories(test_message_assembler PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
add_test(NAME MessageAssembler COMMAND test_message_assembler)

add_executable(test_solar_position tests/lqa/unit/test_solar_position.cpp)
target_link_libraries(test_solar_position ale_lqa ale_protocol ale_fsk_core ale_fec)
target_include_directories(test_solar_position PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
add_test(NAME SolarPosition COMMAND test_solar_position)

add_executable(test_propagation_scoring tests/lqa/unit/test_propagation_scoring.cpp)
target_link_libraries(test_propagation_scoring ale_lqa ale_protocol ale_fsk_core ale_fec)
target_include_directories(test_propagation_scoring PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
add_test(NAME PropagationScoring COMMAND test_propagation_scoring)

add_executable(test_gps_parsers tests/chan/unit/test_gps_parsers.cpp)
target_link_libraries(test_gps_parsers ale_app_core ale_lqa ale_link ale_modem ale_protocol ale_fsk_core ale_fec)
target_include_directories(test_gps_parsers PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
add_test(NAME GpsParsers COMMAND test_gps_parsers)

add_executable(test_sfi_service tests/chan/unit/test_sfi_service.cpp)
target_link_libraries(test_sfi_service ale_app_core ale_lqa ale_link ale_modem ale_protocol ale_fsk_core ale_fec)
target_include_directories(test_sfi_service PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
add_test(NAME SfiService COMMAND test_sfi_service)

# Regression: hamlib lock-probe UB must stay neutralized (HamlibRadio::start()),
# and the mode re-assert backstop must converge against a Quisk-style server
# whose band memory reverts the mode asynchronously after a frequency change.
# Real pal::HamlibRadio over NET_RIGCTL against an in-process mock — needs hamlib.
if(HAMLIB_FOUND)
    add_executable(test_mode_reassert tests/radio/unit/test_mode_reassert.cpp)
    target_link_libraries(test_mode_reassert ale_pal)
    if(WIN32)
        target_link_libraries(test_mode_reassert ws2_32)
    endif()
    add_test(NAME ModeReassert COMMAND test_mode_reassert)

    # Scan dwell timing + stop-on-detection over the REAL async netrigctl TCP path.
    # Real pal::HamlibRadio (NET_RIGCTL) + ALEController scanning against a
    # latency-injecting in-process rigctld mock.
    add_executable(test_scan_dwell_tcp tests/radio/integration/test_scan_dwell_tcp.cpp)
    target_link_libraries(test_scan_dwell_tcp
        ale_app_core ale_lqa ale_link ale_modem ale_protocol ale_fsk_core ale_fec ale_pal)
    target_include_directories(test_scan_dwell_tcp PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    if(WIN32)
        target_link_libraries(test_scan_dwell_tcp ws2_32)
    endif()
    add_test(NAME ScanDwellTcp COMMAND test_scan_dwell_tcp)
endif()

# Temporary repro harness (brief-TX investigation) — not registered as a test.
add_executable(repro_brief_tx tests/link/integration/repro_brief_tx.cpp)
target_link_libraries(repro_brief_tx ale_app_core ale_lqa ale_link ale_modem ale_protocol ale_fsk_core ale_fec)
target_include_directories(repro_brief_tx PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

# Temporary probe (brief-TX investigation): real WASAPI completion pacing.
add_executable(wasapi_pacing_probe tests/link/integration/wasapi_pacing_probe.cpp)
target_link_libraries(wasapi_pacing_probe ale_audio_device ale_modem ale_fsk_core ale_pal)
target_include_directories(wasapi_pacing_probe PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

# ── Examples ──────────────────────────────────────────────────────────────────

option(BUILD_EXAMPLES "Build example programs" ON)
if(BUILD_EXAMPLES)
    add_executable(example_ale_decoder examples/example_ale_decoder.cpp)
    target_link_libraries(example_ale_decoder ale_stores ale_protocol ale_fsk_core ale_fec)
    target_include_directories(example_ale_decoder PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

    add_executable(example_complete_stack examples/example_complete_stack.cpp)
    target_link_libraries(example_complete_stack ale_link ale_protocol ale_fsk_core ale_fec)
    target_include_directories(example_complete_stack PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

    add_executable(example_aqc_usage examples/example_aqc_usage.cpp)
    target_link_libraries(example_aqc_usage ale_aqc ale_protocol ale_fsk_core ale_fec)
    target_include_directories(example_aqc_usage PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

    add_executable(example_fs1052_transfer examples/example_fs1052_transfer.cpp)
    target_link_libraries(example_fs1052_transfer ale_fs1052 ale_protocol ale_fsk_core ale_fec)
    target_include_directories(example_fs1052_transfer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

    # Diagnostic tool: dump TX audio (generator/resampler) to WAV for offline inspection.
    add_executable(tx_wav_dump tools/tx_wav_dump.cpp)
    target_link_libraries(tx_wav_dump ale_link ale_modem ale_protocol ale_fsk_core ale_fec)
    target_include_directories(tx_wav_dump PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
endif()

# ── ALE Monitor (passive RX-only traffic monitor) ────────────────────────────
# Guard: CMAKE_SOURCE_DIR == CMAKE_CURRENT_SOURCE_DIR only when this file IS
# the top-level project.  In a standalone ale_monitor build, CMAKE_SOURCE_DIR
# is ale_monitor/ — the equality fails, preventing infinite re-entry.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    add_subdirectory(ale_monitor)
endif()
