cmake_minimum_required(VERSION 3.20)
project(QwenEchoEngine VERSION 0.1.0 LANGUAGES C CXX)

# ─── C++ Standard ────────────────────────────────────────────────────────────
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# ─── Architecture ────────────────────────────────────────────────────────────
# Target arm64 only (arm64-v8a on Android, arm64 on iOS/macOS).
# For cross-compilation, pass the appropriate toolchain file.
if(NOT CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "Target architecture")
endif()

# ─── Compiler Warnings ───────────────────────────────────────────────────────
add_compile_options(
    -Wall -Wextra -Wpedantic
    -Wno-unused-parameter
)

# ─── Include Directories ─────────────────────────────────────────────────────
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/hal)

# ─── Source Files ────────────────────────────────────────────────────────────
# Collect all source files (added incrementally as components are implemented)
file(GLOB_RECURSE ENGINE_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c"
)

# ─── Platform HAL Sources ────────────────────────────────────────────────────
if(ANDROID)
    file(GLOB ANDROID_HAL_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/hal/android/*.c"
    )
    list(APPEND ENGINE_SOURCES ${ANDROID_HAL_SOURCES})
endif()

# Only create the library target if source files exist
if(ENGINE_SOURCES)
    add_library(qwen_echo_engine STATIC ${ENGINE_SOURCES})
    target_include_directories(qwen_echo_engine PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/hal
    )

    # ─── Android-specific Configuration ──────────────────────────────────────
    if(ANDROID)
        # Link against required Android libraries
        target_link_libraries(qwen_echo_engine PUBLIC
            aaudio
            android
            log
        )

        # Android 15+ (API 35+) requires 16KB page size alignment.
        # This linker flag ensures the shared library is compatible with
        # devices using 16KB pages.
        target_link_options(qwen_echo_engine PUBLIC
            "-Wl,-z,max-page-size=16384"
        )
    endif()
endif()

# ─── Testing ─────────────────────────────────────────────────────────────────
option(QWEN_ECHO_BUILD_TESTS "Build property-based tests with RapidCheck" ON)

if(QWEN_ECHO_BUILD_TESTS)
    enable_testing()

    include(FetchContent)

    # RapidCheck - property-based testing framework for C++
    FetchContent_Declare(
        rapidcheck
        GIT_REPOSITORY https://github.com/emil-e/rapidcheck.git
        GIT_TAG        master
    )
    FetchContent_MakeAvailable(rapidcheck)

    # Create a separate test executable for each test source file
    file(GLOB_RECURSE TEST_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp"
    )

    foreach(TEST_SRC ${TEST_SOURCES})
        # Extract test name from filename (e.g., test_echo_types.cpp → test_echo_types)
        get_filename_component(TEST_NAME ${TEST_SRC} NAME_WE)

        # Tests that provide their own mocks for HAL/native_port need to link
        # only with the specific source under test, not the full engine library.
        if(${TEST_NAME} STREQUAL "test_audio_collector")
            add_executable(${TEST_NAME}
                ${TEST_SRC}
                ${CMAKE_CURRENT_SOURCE_DIR}/src/audio_collector.cpp
            )
            target_include_directories(${TEST_NAME} PRIVATE
                ${CMAKE_CURRENT_SOURCE_DIR}/include
                ${CMAKE_CURRENT_SOURCE_DIR}/hal
                ${CMAKE_CURRENT_SOURCE_DIR}/src
            )
            target_link_libraries(${TEST_NAME} PRIVATE rapidcheck)
        else()
            add_executable(${TEST_NAME} ${TEST_SRC})
            target_include_directories(${TEST_NAME} PRIVATE
                ${CMAKE_CURRENT_SOURCE_DIR}/include
                ${CMAKE_CURRENT_SOURCE_DIR}/hal
                ${CMAKE_CURRENT_SOURCE_DIR}/src
            )
            target_link_libraries(${TEST_NAME} PRIVATE rapidcheck)

            # Link against the engine library if it exists
            if(TARGET qwen_echo_engine)
                target_link_libraries(${TEST_NAME} PRIVATE qwen_echo_engine)
            endif()
        endif()

        add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
    endforeach()
endif()
