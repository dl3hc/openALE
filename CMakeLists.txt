cmake_minimum_required(VERSION 3.15)
project(ALE-Clean-Room VERSION 2.0.0 LANGUAGES CXX)

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

# Modem / FSK layer
add_library(ale_fsk_core
    src/FSK/fft_demodulator.cpp
    src/FSK/tone_generator.cpp
    src/FSK/symbol_decoder.cpp
    src/FSK/ale_waveform.cpp
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
    src/Protocol/ale_orderwire_protocols.cpp
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
    src/Modem/ale2g_modem.cpp   # Modulator + Demodulator
)

target_include_directories(ale_modem PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(ale_modem ale_codec ale_protocol ale_fsk_core ale_fec)

# ALE state machine (link layer)
add_library(ale_link
    src/Protocol/Control/ale_state_machine.cpp
    src/Protocol/Control/ale_channel_manager.cpp
    src/Word/ale_word_decoder.cpp
)

target_include_directories(ale_link PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(ale_link ale_stores ale_protocol ale_fsk_core ale_fec)

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
    src/LQA/lqa_metrics.cpp
    src/LQA/lqa_analyzer.cpp
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
# CAT/PTT deaktivieren: cmake -DHAMLIB_BUILD=OFF

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

    if(NOT HAMLIB_FOUND)
        message(STATUS "Hamlib nicht gefunden — Radio CAT/PTT deaktiviert")
        message(STATUS "  Einmalig bauen (MSYS2 MinGW64-Shell):")
        message(STATUS "    bash scripts/build_hamlib.sh")
    endif()
endif()

add_library(ale_pal
    src/PAL/timer.cpp
    src/PAL/radio.cpp
)

target_include_directories(ale_pal PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

if(WIN32)
    target_link_libraries(ale_pal winmm)
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
)

target_include_directories(ale_app_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(ale_app_core
    ale_link ale_modem ale_protocol ale_fsk_core ale_fec
)

# ── Audio I/O device (platform-specific) ──────────────────────────────────────

add_library(ale_audio_device
    src/App/audio_device.cpp
)

target_include_directories(ale_audio_device PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

if(WIN32)
    # WASAPI (mmdeviceapi/audioclient) + COM device enumeration.
    # ole32: COM; uuid: PKEY_Device_FriendlyName property key.
    target_link_libraries(ale_audio_device ole32 uuid)
endif()

# ── ALE CLI executable ────────────────────────────────────────────────────────

add_executable(ale_cli apps/ale_cli.cpp)

# ── Mock Radio Server (rigctld-kompatibel, kein Hamlib nötig) ─────────────────

add_executable(radio_mock apps/radio_mock.cpp)

target_include_directories(radio_mock PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

if(WIN32)
    target_link_libraries(radio_mock ws2_32)
endif()

target_include_directories(ale_cli PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(ale_cli ale_app_core ale_audio_device ale_stores ale_pal)

# ── WebSocket bridge (GUI↔Core) ────────────────────────────────────────────────

add_executable(ale_bridge apps/ale_bridge.cpp apps/bridge/ws_server.cpp)

target_include_directories(ale_bridge PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/apps
)

target_link_libraries(ale_bridge ale_app_core ale_audio_device ale_stores ale_pal)

if(WIN32)
    target_link_libraries(ale_bridge ws2_32)
endif()

# DLL-Build: libhamlib-4.dll + Runtime-DLLs neben ale_cli.exe kopieren
# (muss nach add_executable stehen)
if(HAMLIB_FOUND AND DEFINED _hamlib_bin)
    file(GLOB _hamlib_dlls "${_hamlib_bin}/*.dll")
    foreach(_dll ${_hamlib_dlls})
        add_custom_command(TARGET ale_cli POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${_dll}" "$<TARGET_FILE_DIR:ale_cli>"
        )
    endforeach()
endif()

# ─────────────────────────────────────────────────────────────────────────────

enable_testing()

add_executable(test_fsk_core tests/waveform/unit/test_fsk_core.cpp)
target_link_libraries(test_fsk_core ale_fsk_core ale_fec)
target_include_directories(test_fsk_core PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
add_test(NAME FSKCore COMMAND test_fsk_core)

add_executable(test_fec_golay tests/fec/unit/test_fec_golay.cpp)
target_link_libraries(test_fec_golay ale_fec ale_fsk_core)
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

add_executable(test_address_encoder tests/addr/unit/test_address_encoder.cpp)
target_link_libraries(test_address_encoder ale_protocol ale_fsk_core ale_fec)
target_include_directories(test_address_encoder PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
add_test(NAME AddressEncoder COMMAND test_address_encoder)

add_executable(test_ale_calling tests/word/unit/test_ale_calling.cpp)
target_link_libraries(test_ale_calling ale_link ale_protocol ale_fsk_core ale_fec)
target_include_directories(test_ale_calling PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
add_test(NAME ALECalling COMMAND test_ale_calling)

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

add_executable(test_decoder_robustness tests/sync/integration/test_decoder_robustness.cpp)
target_link_libraries(test_decoder_robustness ale_modem ale_codec ale_protocol ale_fsk_core ale_fec)
target_include_directories(test_decoder_robustness PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
add_test(NAME DecoderRobustness COMMAND test_decoder_robustness)

add_executable(test_architecture tests/gen/integration/test_layering.cpp)
target_compile_definitions(test_architecture PRIVATE PROJECT_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
add_test(NAME Architecture COMMAND test_architecture)

add_executable(debug_fft tests/debug_fft.cpp)
target_link_libraries(debug_fft ale_fsk_core ale_fec)
target_include_directories(debug_fft PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

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
endif()
