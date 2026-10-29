# Copyright (c) 2026 AlkaidLab contributors
# SPDX-License-Identifier: MIT AND GPL-3.0-only

cmake_minimum_required(VERSION 3.20)
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_C_STANDARD 99)
project(pyrowave LANGUAGES CXX C)
include(GNUInstallDirs)

set(PYROWAVE_IS_TOP_LEVEL OFF)
if (CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    set(PYROWAVE_IS_TOP_LEVEL ON)
endif()

set(PYROWAVE_GRANITE_SOURCE_DIR
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/Granite"
        CACHE PATH "Granite source tree used by PyroWave")
if (NOT EXISTS "${PYROWAVE_GRANITE_SOURCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR
            "Granite is missing. Initialize the PyroWave third-party submodule "
            "with git submodule update --init --recursive.")
endif()

if (CMAKE_COMPILER_IS_GNUCXX OR (${CMAKE_CXX_COMPILER_ID} MATCHES "Clang"))
    set(PYROWAVE_CXX_FLAGS -Wshadow -Wall -Wextra -Wno-comment -Wno-missing-field-initializers -Wno-empty-body -fvisibility=hidden)
    if (${CMAKE_CXX_COMPILER_ID} MATCHES "Clang")
        set(PYROWAVE_CXX_FLAGS ${PYROWAVE_CXX_FLAGS} -Wno-backslash-newline-escape)
    endif()
    if (NOT (${CMAKE_BUILD_TYPE} MATCHES "Release"))
        message("Enabling frame pointer for profiling/debug.")
        set(PYROWAVE_CXX_FLAGS ${PYROWAVE_CXX_FLAGS} -fno-omit-frame-pointer)
    endif()
    if (CMAKE_SYSTEM_PROCESSOR MATCHES "(x86)|(X86)|(amd64)|(AMD64)")
        message("Enabling SSE3 support.")
        set(PYROWAVE_CXX_FLAGS ${PYROWAVE_CXX_FLAGS} -msse3)
    endif()
elseif (MSVC)
    set(PYROWAVE_CXX_FLAGS /D_CRT_SECURE_NO_WARNINGS /wd4267 /wd4244 /wd4309 /wd4005 /MP)
endif()

add_library(pyrowave STATIC
        pyrowave_config.hpp shaders/slangmosh.hpp
        pyrowave_encoder.hpp pyrowave_encoder.cpp
        pyrowave_decoder.hpp pyrowave_decoder.cpp
        pyrowave_common.hpp pyrowave_common.cpp)

add_library(pyrowave-regression-results INTERFACE)
target_include_directories(pyrowave-regression-results INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/eval-results)

target_include_directories(pyrowave PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(pyrowave PRIVATE ${PYROWAVE_CXX_FLAGS})
set_target_properties(pyrowave PROPERTIES POSITION_INDEPENDENT_CODE ON)

option(PYROWAVE_DEVEL "Build PyroWave as a standalone sandbox with development tooling." OFF)
option(PYROWAVE_UTILS "Build PyroWave with extra runtime tooling." OFF)
option(PYROWAVE_BUILD_SHARED "Build the shared C API runtime." ON)
option(PYROWAVE_BUILD_STATIC_C_API "Build a static C API runtime." OFF)
option(PYROWAVE_BUILD_TESTS "Build the C API tests and device validation tool." ${PYROWAVE_IS_TOP_LEVEL})
option(PYROWAVE_INSTALL "Add PyroWave install rules." ${PYROWAVE_IS_TOP_LEVEL})

option(PYROWAVE_FP32_STORAGE "Configure for full FP32 and extended precision." OFF)
option(PYROWAVE_FP32_MATH "Configure for FP32 arithmetic, but reduced range storage." ON)
if (PYROWAVE_FP32_STORAGE)
    target_compile_definitions(pyrowave PRIVATE PYROWAVE_PRECISION=2)
elseif(PYROWAVE_FP32_MATH)
    target_compile_definitions(pyrowave PRIVATE PYROWAVE_PRECISION=1)
else()
    target_compile_definitions(pyrowave PRIVATE PYROWAVE_PRECISION=0)
endif()

add_library(pyrowave-utils STATIC yuv4mpeg.cpp yuv4mpeg.hpp)
target_compile_options(pyrowave-utils PRIVATE ${PYROWAVE_CXX_FLAGS})
target_include_directories(pyrowave-utils PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

if (PYROWAVE_UTILS)
    set(GRANITE_FFMPEG ON CACHE BOOL "" FORCE)
    set(GRANITE_FFMPEG_VULKAN ON CACHE BOOL "" FORCE)
else()
    set(GRANITE_FFMPEG OFF CACHE BOOL "" FORCE)
    set(GRANITE_FFMPEG_VULKAN OFF CACHE BOOL "" FORCE)
endif()

# Granite configures its static dependencies through the global cache. Do not
# change the embedding project's library preference.
if (DEFINED CACHE{BUILD_SHARED_LIBS})
    set(_pyrowave_had_shared_cache ON)
    get_property(_pyrowave_shared_cache_value CACHE BUILD_SHARED_LIBS PROPERTY VALUE)
    get_property(_pyrowave_shared_cache_type CACHE BUILD_SHARED_LIBS PROPERTY TYPE)
    get_property(_pyrowave_shared_cache_help CACHE BUILD_SHARED_LIBS PROPERTY HELPSTRING)
else()
    set(_pyrowave_had_shared_cache OFF)
endif()

if (PYROWAVE_DEVEL)
    # Standalone build, build the sandbox.
    set(GRANITE_RENDERER ON CACHE BOOL "" FORCE)
    set(GRANITE_VULKAN_FOSSILIZE ON CACHE BOOL "" FORCE)
    set(GRANITE_SHIPPING OFF CACHE BOOL "" FORCE)
    if (ANDROID)
        set(GRANITE_PLATFORM "headless" CACHE STRING "" FORCE)
		set(GRANITE_VULKAN_FOSSILIZE OFF CACHE BOOL "" FORCE)
    else()
        set(GRANITE_PLATFORM "SDL" CACHE STRING "" FORCE)
    endif()
    set(GRANITE_HIDDEN ON CACHE BOOL "" FORCE)
    set(GRANITE_POSITION_INDEPENDENT ON CACHE BOOL "" FORCE)
    set(GRANITE_VULKAN_SPIRV_CROSS ON CACHE BOOL "" FORCE)
    set(GRANITE_VULKAN_SYSTEM_HANDLES ON CACHE BOOL "" FORCE)
    add_subdirectory("${PYROWAVE_GRANITE_SOURCE_DIR}"
            "${CMAKE_CURRENT_BINARY_DIR}/Granite" EXCLUDE_FROM_ALL)

    add_granite_offline_tool(pyrowave-sandbox sandbox.cpp)
    target_link_libraries(pyrowave-sandbox PRIVATE pyrowave granite-renderer pyrowave-utils)
    target_compile_definitions(pyrowave-sandbox PRIVATE ASSET_DIRECTORY=\"${CMAKE_CURRENT_SOURCE_DIR}/shaders\")

    add_granite_offline_tool(pyrowave-bench bench.cpp)
    target_link_libraries(pyrowave-bench PRIVATE pyrowave)
    target_link_libraries(pyrowave-bench PRIVATE granite-vulkan pyrowave-utils)

    add_granite_offline_tool(pyrowave-encode encode.cpp)
    target_link_libraries(pyrowave-encode PRIVATE pyrowave)
    target_link_libraries(pyrowave-encode PRIVATE granite-vulkan pyrowave-utils)

    add_granite_offline_tool(pyrowave-decode decode.cpp)
    target_link_libraries(pyrowave-decode PRIVATE pyrowave)
    target_link_libraries(pyrowave-decode PRIVATE granite-vulkan pyrowave-utils)

    add_granite_application(pyrowave-viewer viewer.cpp)
    target_link_libraries(pyrowave-viewer PRIVATE pyrowave pyrowave-utils)
    if (NOT ANDROID)
        target_compile_definitions(pyrowave-viewer PRIVATE ASSET_DIRECTORY=\"${CMAKE_CURRENT_SOURCE_DIR}/shaders\")
    endif()
elseif (NOT TARGET granite-vulkan)
    set(GRANITE_VULKAN_SHADER_MANAGER_RUNTIME_COMPILER OFF CACHE BOOL "" FORCE)
    set(GRANITE_SHADER_COMPILER_OPTIMIZE OFF CACHE BOOL "" FORCE)
    set(GRANITE_POSITION_INDEPENDENT ON CACHE BOOL "" FORCE)
    set(GRANITE_SHIPPING ON CACHE BOOL "" FORCE)
    if (PYROWAVE_UTILS)
        # Should normally be OFF, but slangmosh doesn't jive well with Fossilize.
        set(GRANITE_VULKAN_SPIRV_CROSS ON CACHE BOOL "" FORCE)
        set(GRANITE_RENDERER ON CACHE BOOL "" FORCE)
        set(GRANITE_VULKAN_FOSSILIZE ON CACHE BOOL "" FORCE)
        set(GRANITE_PLATFORM "SDL" CACHE STRING "" FORCE)
        set(GRANITE_VULKAN_SYSTEM_HANDLES ON CACHE BOOL "" FORCE)
    else()
        set(GRANITE_VULKAN_SPIRV_CROSS OFF CACHE BOOL "" FORCE)
        set(GRANITE_VULKAN_SYSTEM_HANDLES OFF CACHE BOOL "" FORCE)
        set(GRANITE_RENDERER OFF CACHE BOOL "" FORCE)
        set(GRANITE_VULKAN_FOSSILIZE OFF CACHE BOOL "" FORCE)
        set(GRANITE_PLATFORM "null" CACHE STRING "" FORCE)
    endif()
    add_subdirectory("${PYROWAVE_GRANITE_SOURCE_DIR}"
            "${CMAKE_CURRENT_BINARY_DIR}/Granite" EXCLUDE_FROM_ALL)
endif()

if (_pyrowave_had_shared_cache)
    set(BUILD_SHARED_LIBS "${_pyrowave_shared_cache_value}" CACHE
            ${_pyrowave_shared_cache_type} "${_pyrowave_shared_cache_help}" FORCE)
else()
    unset(BUILD_SHARED_LIBS CACHE)
endif()

target_link_libraries(pyrowave PRIVATE granite-vulkan granite-math)

if (PYROWAVE_BUILD_TESTS)
    enable_testing()
    add_executable(pyrowave-native-fp16-test pyrowave_native_fp16_test.cpp pyrowave_features.hpp)
    target_link_libraries(pyrowave-native-fp16-test PRIVATE granite-volk-headers)
    target_compile_options(pyrowave-native-fp16-test PRIVATE ${PYROWAVE_CXX_FLAGS})
    add_test(NAME pyrowave-native-fp16-test COMMAND pyrowave-native-fp16-test)
endif()

if (PYROWAVE_IS_TOP_LEVEL AND NOT PYROWAVE_BUILD_SHARED AND NOT PYROWAVE_BUILD_STATIC_C_API)
    message(FATAL_ERROR "At least one PyroWave C API runtime must be enabled")
endif()

if (PYROWAVE_BUILD_TESTS AND NOT PYROWAVE_BUILD_SHARED AND NOT PYROWAVE_BUILD_STATIC_C_API)
    message(FATAL_ERROR "PyroWave C API tests require a C API runtime")
endif()

if (PYROWAVE_BUILD_SHARED OR PYROWAVE_BUILD_STATIC_C_API)

    set(PYROWAVE_C_API_SOURCES
            pyrowave_c.cpp pyrowave.h
            "${PYROWAVE_GRANITE_SOURCE_DIR}/video/scaler.cpp"
            "${PYROWAVE_GRANITE_SOURCE_DIR}/video/scaler.hpp"
            shaders/slangmosh_scaler.hpp)

    if (PYROWAVE_BUILD_SHARED)
        add_library(pyrowave-shared SHARED ${PYROWAVE_C_API_SOURCES})
        target_compile_definitions(pyrowave-shared PRIVATE PYROWAVE_EXPORT_SYMBOLS)
        target_compile_options(pyrowave-shared PRIVATE ${PYROWAVE_CXX_FLAGS})
        target_include_directories(pyrowave-shared PRIVATE
                shaders "${PYROWAVE_GRANITE_SOURCE_DIR}/video")
        target_link_libraries(pyrowave-shared PRIVATE pyrowave granite-vulkan granite-math)
    endif()

    if (PYROWAVE_BUILD_STATIC_C_API)
        add_library(pyrowave-c-api-static STATIC ${PYROWAVE_C_API_SOURCES})
        target_compile_options(pyrowave-c-api-static PRIVATE ${PYROWAVE_CXX_FLAGS})
        target_include_directories(pyrowave-c-api-static PRIVATE
                shaders "${PYROWAVE_GRANITE_SOURCE_DIR}/video")
        target_include_directories(pyrowave-c-api-static PUBLIC
                "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>"
                "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/pyrowave>")
        target_link_libraries(pyrowave-c-api-static PRIVATE pyrowave granite-vulkan granite-math)
        target_link_libraries(pyrowave-c-api-static PUBLIC granite-volk-headers)
    endif()

    if (PYROWAVE_BUILD_STATIC_C_API AND NOT PYROWAVE_BUILD_SHARED)
        set(PYROWAVE_C_API_TARGET pyrowave-c-api-static)
    else()
        set(PYROWAVE_C_API_TARGET pyrowave-shared)
    endif()

    if (PYROWAVE_BUILD_TESTS)
        add_executable(pyrowave-c-test pyrowave_c_test.cpp)
        target_link_libraries(pyrowave-c-test PRIVATE ${PYROWAVE_C_API_TARGET} granite-volk-headers)
        target_compile_options(pyrowave-c-test PRIVATE ${PYROWAVE_CXX_FLAGS})

        add_executable(pyrowave-c-interop-test pyrowave_c_interop_test.cpp com_ptr.hpp)
        target_link_libraries(pyrowave-c-interop-test PRIVATE ${PYROWAVE_C_API_TARGET} granite-vulkan)
        target_compile_options(pyrowave-c-interop-test PRIVATE ${PYROWAVE_CXX_FLAGS})

        add_executable(pyrowave-device-validation pyrowave_device_validation.cpp com_ptr.hpp)
        target_link_libraries(pyrowave-device-validation PRIVATE ${PYROWAVE_C_API_TARGET} granite-util granite-volk-headers)
        target_compile_options(pyrowave-device-validation PRIVATE ${PYROWAVE_CXX_FLAGS})
    endif()

    set(PYROWAVE_API_VERSION_MAJOR 101)
    set(PYROWAVE_API_VERSION_MINOR 0)
    set(PYROWAVE_API_VERSION_PATCH 0)
    set(PYROWAVE_API_VERSION ${PYROWAVE_API_VERSION_MAJOR}.${PYROWAVE_API_VERSION_MINOR}.${PYROWAVE_API_VERSION_PATCH})

    # DLL_NAME_WITH_SOVERSION requires CMake 3.27+. SteamRT doesn't have CMake 3.27 yet though.
    if (WIN32 AND PYROWAVE_BUILD_SHARED AND (CMAKE_VERSION VERSION_LESS "3.27"))
        message(FATAL_ERROR "CMake build on Windows expects version 3.27 at minimum.")
    endif()

    if (PYROWAVE_BUILD_SHARED)
        set_target_properties(pyrowave-shared
                PROPERTIES VERSION ${PYROWAVE_API_VERSION}
                SOVERSION ${PYROWAVE_API_VERSION_MAJOR}
                DLL_NAME_WITH_SOVERSION ON)
    endif()

    if (PYROWAVE_INSTALL)
        install(FILES ${CMAKE_CURRENT_SOURCE_DIR}/pyrowave.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/pyrowave)
    endif()

    if (PYROWAVE_INSTALL AND PYROWAVE_BUILD_SHARED)
        install(TARGETS pyrowave-shared EXPORT pyrowave-sharedConfig
                RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
                LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
                ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
                PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/pyrowave)
        install(EXPORT pyrowave-sharedConfig DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/pyrowave-shared/cmake)
    endif()
    if (PYROWAVE_INSTALL AND PYROWAVE_BUILD_STATIC_C_API)
        include(cmake/InstallStaticCAPI.cmake)
        pyrowave_install_static_c_api()
    endif()
    if (PYROWAVE_INSTALL AND PYROWAVE_BUILD_TESTS)
        install(TARGETS pyrowave-device-validation RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    endif()

    # Evaluator app
    if (PYROWAVE_UTILS)
        add_granite_application(pyrowave-evaluator evaluator.cpp)
        target_link_libraries(pyrowave-evaluator PRIVATE granite-video pyrowave-utils pyrowave)
        if (PYROWAVE_DEVEL AND (NOT ANDROID))
            target_compile_definitions(pyrowave-evaluator PRIVATE ASSET_DIRECTORY=\"${CMAKE_CURRENT_SOURCE_DIR}/shaders\")
        endif()
        if (PYROWAVE_INSTALL)
            install(TARGETS pyrowave-evaluator RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
            install(DIRECTORY assets DESTINATION ${CMAKE_INSTALL_BINDIR})
            install(DIRECTORY "${PYROWAVE_GRANITE_SOURCE_DIR}/assets/fonts"
                    DESTINATION ${CMAKE_INSTALL_BINDIR}/builtin/assets)
        endif()

        add_granite_offline_tool(pyrowave-psnr-hvs-m psnr.cpp)
        target_link_libraries(pyrowave-psnr-hvs-m PRIVATE granite-vulkan pyrowave-utils pyrowave granite-video)
        target_compile_definitions(pyrowave-psnr-hvs-m PRIVATE ASSET_DIRECTORY=\"${CMAKE_CURRENT_SOURCE_DIR}/shaders\")
    endif()

    if (PYROWAVE_INSTALL AND PYROWAVE_BUILD_SHARED)
        set(PYROWAVE_INSTALL_LIB_DIR ${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR})
        set(PYROWAVE_INSTALL_INC_DIR ${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_INCLUDEDIR}/pyrowave)
        configure_file(${CMAKE_CURRENT_SOURCE_DIR}/pkg-config/pyrowave-shared.pc.in
                ${CMAKE_CURRENT_BINARY_DIR}/pyrowave-shared.pc @ONLY)
        install(FILES ${CMAKE_CURRENT_BINARY_DIR}/pyrowave-shared.pc DESTINATION ${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}/pkgconfig)
    endif()

    if (WIN32 AND CMAKE_COMPILER_IS_GNUCXX)
        if (PYROWAVE_BUILD_SHARED)
            target_link_libraries(pyrowave-shared PRIVATE -static gcc stdc++ winpthread)
        endif()
        if (PYROWAVE_BUILD_STATIC_C_API)
            target_link_libraries(pyrowave-c-api-static PRIVATE -static gcc stdc++ winpthread)
        endif()
        if (PYROWAVE_BUILD_TESTS)
            target_link_libraries(pyrowave-c-test PRIVATE -static gcc stdc++ winpthread)
            target_link_libraries(pyrowave-c-interop-test PRIVATE -static gcc stdc++ winpthread)
            target_link_libraries(pyrowave-device-validation PRIVATE -static gcc stdc++ winpthread)
        endif()
    endif()

    if (NOT WIN32)
        if (PYROWAVE_BUILD_SHARED)
            target_link_libraries(pyrowave-shared PRIVATE -pthread)
        endif()
        if (PYROWAVE_BUILD_STATIC_C_API)
            target_link_libraries(pyrowave-c-api-static PRIVATE -pthread)
        endif()
        if (PYROWAVE_BUILD_TESTS)
            target_link_libraries(pyrowave-c-test PRIVATE -pthread)
            target_link_libraries(pyrowave-c-interop-test PRIVATE -pthread)
            target_link_libraries(pyrowave-device-validation PRIVATE -pthread)
        endif()
    endif()

    if (WIN32)
        if (PYROWAVE_BUILD_SHARED)
            target_sources(pyrowave-shared PRIVATE pyrowave-shared.def)
            set_target_properties(pyrowave-shared PROPERTIES PREFIX "lib")
        endif()
        if (PYROWAVE_BUILD_TESTS)
            target_link_libraries(pyrowave-c-interop-test PRIVATE d3d11 d3d12 dxgi)
        endif()
    elseif (NOT APPLE)
        if (PYROWAVE_BUILD_SHARED)
            target_link_options(pyrowave-shared PRIVATE -Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/link.T)
        endif()
    endif()
endif()

if (PYROWAVE_DEVEL AND WIN32)
    add_granite_offline_tool(pyrowave-encode-desktop encode_desktop.cpp com_ptr.hpp)
    target_link_libraries(pyrowave-encode-desktop PRIVATE ${PYROWAVE_C_API_TARGET} dxgi d3d11 pyrowave-utils)
endif()

