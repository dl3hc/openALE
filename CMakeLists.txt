cmake_minimum_required(VERSION 3.15)
project(ALE-Clean-Room VERSION 2.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    # MSVC sets /Od /Zi (Debug) and /O2 /DNDEBUG (Release) automatically per build type.
    # /bigobj is required for large translation units; /W4 enables high-level warnings.
    add_compile_options(/bigobj /W4)
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

# ALE 2G Modem — word-level TX with 3× redundancy (A.5.2.2.4)
add_library(ale_modem
    src/Modem/ale2g_modem.cpp
)

target_include_directories(ale_modem PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(ale_modem ale_protocol ale_fsk_core ale_fec)

# ALE state machine (link layer)
add_library(ale_link
    src/Protocol/Control/ale_state_machine.cpp
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

# ── Tests ─────────────────────────────────────────────────────────────────────

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

add_executable(test_ale_calling tests/test_ale_calling.cpp)
target_link_libraries(test_ale_calling ale_link ale_protocol ale_fsk_core ale_fec)
target_include_directories(test_ale_calling PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
add_test(NAME ALECalling COMMAND test_ale_calling)

add_executable(test_frame tests/test_frame.cpp)
target_link_libraries(test_frame ale_link ale_modem ale_protocol ale_fsk_core ale_fec)
target_include_directories(test_frame PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
add_test(NAME Frame COMMAND test_frame)

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
