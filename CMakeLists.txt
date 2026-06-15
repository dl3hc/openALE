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
    src/FEC/interleaver.cpp
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

# Optional: Hamlib radio control (CAT + PTT)
#
# Windows (bundled — recommended):
#   Copy Hamlib DLLs to deps/hamlib/bin/, headers to deps/hamlib/include/,
#   import library to deps/hamlib/lib/.  CMake picks them up automatically
#   and copies the DLLs next to the .exe so no PATH setup is required.
#
# Windows (external install):
#   cmake -DHAMLIB_ROOT=C:/hamlib ..   or set HAMLIB_ROOT env var
#
# Linux:
#   sudo apt install libhamlib-dev   (detected via pkg-config automatically)
set(HAMLIB_FOUND FALSE)

# 1. Check bundled deps/ directory first (highest priority on all platforms)
set(_hamlib_bundled "${CMAKE_CURRENT_SOURCE_DIR}/deps/hamlib")
if(EXISTS "${_hamlib_bundled}/include/hamlib/rig.h")
    find_library(HAMLIB_LIBRARY NAMES hamlib libhamlib
        HINTS "${_hamlib_bundled}/lib" NO_DEFAULT_PATH)
    if(HAMLIB_LIBRARY)
        set(HAMLIB_FOUND TRUE)
        set(HAMLIB_LIBRARIES    "${HAMLIB_LIBRARY}")
        set(HAMLIB_INCLUDE_DIRS "${_hamlib_bundled}/include")
        set(HAMLIB_BIN_DIR      "${_hamlib_bundled}/bin")
        message(STATUS "Hamlib found (bundled in deps/hamlib)")
    endif()
endif()

# 2. pkg-config (Linux / MSYS2)
if(NOT HAMLIB_FOUND)
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(HAMLIB QUIET hamlib)
        if(HAMLIB_FOUND)
            message(STATUS "Hamlib found via pkg-config")
        endif()
    endif()
endif()

# 3. Manual HAMLIB_ROOT (CMake variable or env var)
if(NOT HAMLIB_FOUND)
    set(_hamlib_hints
        "$ENV{HAMLIB_ROOT}/lib"
        "${HAMLIB_ROOT}/lib"
    )
    find_library(HAMLIB_LIBRARY NAMES hamlib libhamlib HINTS ${_hamlib_hints})
    find_path(HAMLIB_INCLUDE_DIR NAMES hamlib/rig.h
        HINTS "$ENV{HAMLIB_ROOT}/include" "${HAMLIB_ROOT}/include")
    if(HAMLIB_LIBRARY AND HAMLIB_INCLUDE_DIR)
        set(HAMLIB_FOUND TRUE)
        set(HAMLIB_LIBRARIES    "${HAMLIB_LIBRARY}")
        set(HAMLIB_INCLUDE_DIRS "${HAMLIB_INCLUDE_DIR}")
        # DLLs expected alongside the lib on Windows
        get_filename_component(_hamlib_lib_dir "${HAMLIB_LIBRARY}" DIRECTORY)
        if(WIN32 AND EXISTS "${_hamlib_lib_dir}/../bin")
            set(HAMLIB_BIN_DIR "${_hamlib_lib_dir}/../bin")
        endif()
        message(STATUS "Hamlib found via HAMLIB_ROOT")
    endif()
endif()

if(NOT HAMLIB_FOUND)
    message(STATUS "Hamlib not found — radio CAT/PTT disabled"
                   "  (copy to deps/hamlib/ or set HAMLIB_ROOT, or: apt install libhamlib-dev)")
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
    target_include_directories(ale_pal PRIVATE ${HAMLIB_INCLUDE_DIRS})
    target_link_libraries(ale_pal ${HAMLIB_LIBRARIES})
    target_compile_definitions(ale_pal PUBLIC HAVE_HAMLIB)

    # On Windows, copy all DLLs from HAMLIB_BIN_DIR next to ale_cli.exe
    # so the program runs without any PATH changes.
    if(WIN32 AND DEFINED HAMLIB_BIN_DIR)
        file(GLOB _hamlib_dlls "${HAMLIB_BIN_DIR}/*.dll")
        foreach(_dll ${_hamlib_dlls})
            add_custom_command(TARGET ale_cli POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${_dll}" "$<TARGET_FILE_DIR:ale_cli>"
                COMMENT "Copying ${_dll} → build output"
            )
        endforeach()
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

# ─────────────────────────────────────────────────────────────────────────────

enable_testing()

add_executable(test_fsk_core tests/test_fsk_core.cpp)
target_link_libraries(test_fsk_core ale_fsk_core ale_fec)
target_include_directories(test_fsk_core PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
add_test(NAME FSKCore COMMAND test_fsk_core)

add_executable(test_fec_golay tests/test_fec_golay.cpp)
target_link_libraries(test_fec_golay ale_fec ale_fsk_core)
target_include_directories(test_fec_golay PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
add_test(NAME FECGolay COMMAND test_fec_golay)

add_executable(test_protocol tests/test_protocol.cpp)
target_link_libraries(test_protocol ale_stores ale_protocol ale_fsk_core ale_fec)
target_include_directories(test_protocol PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
add_test(NAME Protocol COMMAND test_protocol)

add_executable(test_state_machine tests/test_state_machine.cpp)
target_link_libraries(test_state_machine ale_link ale_protocol ale_fsk_core ale_fec)
target_include_directories(test_state_machine PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
add_test(NAME StateMachine COMMAND test_state_machine)

add_executable(test_aqc_parser tests/test_aqc_parser.cpp)
target_link_libraries(test_aqc_parser ale_aqc ale_protocol ale_fsk_core ale_fec)
target_include_directories(test_aqc_parser PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
add_test(NAME AQCParser COMMAND test_aqc_parser)

add_executable(test_aqc_crc tests/test_aqc_crc.cpp)
target_link_libraries(test_aqc_crc ale_aqc ale_protocol ale_fsk_core ale_fec)
target_include_directories(test_aqc_crc PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
add_test(NAME AQCCRC COMMAND test_aqc_crc)

add_executable(test_fs1052_frames tests/test_fs1052_frames.cpp)
target_link_libraries(test_fs1052_frames ale_fs1052 ale_protocol ale_fsk_core ale_fec)
target_include_directories(test_fs1052_frames PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
add_test(NAME FS1052Frames COMMAND test_fs1052_frames)

add_executable(test_fs1052_arq tests/test_fs1052_arq.cpp)
target_link_libraries(test_fs1052_arq ale_fs1052 ale_protocol ale_fsk_core ale_fec)
target_include_directories(test_fs1052_arq PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
add_test(NAME FS1052ARQ COMMAND test_fs1052_arq)

add_executable(test_lqa_database tests/test_lqa_database.cpp)
target_link_libraries(test_lqa_database ale_lqa ale_protocol ale_fsk_core ale_fec)
target_include_directories(test_lqa_database PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
add_test(NAME LQADatabase COMMAND test_lqa_database)

add_executable(test_lqa_metrics tests/test_lqa_metrics.cpp)
target_link_libraries(test_lqa_metrics ale_lqa ale_protocol ale_fsk_core ale_fec)
target_include_directories(test_lqa_metrics PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
add_test(NAME LQAMetrics COMMAND test_lqa_metrics)

add_executable(test_lqa_analyzer tests/test_lqa_analyzer.cpp)
target_link_libraries(test_lqa_analyzer ale_lqa ale_protocol ale_fsk_core ale_fec)
target_include_directories(test_lqa_analyzer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
add_test(NAME LQAAnalyzer COMMAND test_lqa_analyzer)

add_executable(test_address_encoder tests/test_address_encoder.cpp)
target_link_libraries(test_address_encoder ale_protocol ale_fsk_core ale_fec)
target_include_directories(test_address_encoder PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
add_test(NAME AddressEncoder COMMAND test_address_encoder)

add_executable(test_ale_calling tests/test_ale_calling.cpp)
target_link_libraries(test_ale_calling ale_link ale_protocol ale_fsk_core ale_fec)
target_include_directories(test_ale_calling PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
add_test(NAME ALECalling COMMAND test_ale_calling)

add_executable(test_frame tests/test_frame.cpp)
target_link_libraries(test_frame ale_link ale_modem ale_protocol ale_fsk_core ale_fec)
target_include_directories(test_frame PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
add_test(NAME Frame COMMAND test_frame)

add_executable(test_frame_c tests/test_frame_c.cpp)
target_link_libraries(test_frame_c ale_link ale_modem ale_protocol ale_fsk_core ale_fec)
target_include_directories(test_frame_c PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
add_test(NAME FrameC COMMAND test_frame_c)

add_executable(test_resampler_path tests/test_resampler_path.cpp)
target_link_libraries(test_resampler_path ale_app_core ale_link ale_modem ale_protocol ale_fsk_core ale_fec)
target_include_directories(test_resampler_path PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
add_test(NAME ResamplerPath COMMAND test_resampler_path)

add_executable(test_roundtrip tests/test_roundtrip.cpp)
target_link_libraries(test_roundtrip ale_link ale_modem ale_protocol ale_fsk_core ale_fec)
target_include_directories(test_roundtrip PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
add_test(NAME Roundtrip COMMAND test_roundtrip)

add_executable(test_codec tests/test_codec.cpp)
target_link_libraries(test_codec ale_codec ale_protocol ale_fsk_core ale_fec)
target_include_directories(test_codec PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
add_test(NAME Codec COMMAND test_codec)

add_executable(test_pcale_interop tests/test_pcale_interop.cpp)
target_link_libraries(test_pcale_interop ale_codec ale_protocol ale_fsk_core ale_fec)
target_include_directories(test_pcale_interop PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
add_test(NAME PCALEInterop COMMAND test_pcale_interop)

add_executable(test_decoder_robustness tests/test_decoder_robustness.cpp)
target_link_libraries(test_decoder_robustness ale_modem ale_codec ale_protocol ale_fsk_core ale_fec)
target_include_directories(test_decoder_robustness PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
add_test(NAME DecoderRobustness COMMAND test_decoder_robustness)

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
