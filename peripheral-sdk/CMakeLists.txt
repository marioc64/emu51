# SPDX-License-Identifier: LGPL-2.1-only
cmake_minimum_required(VERSION 3.16)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(emu51_peripheral_sdk VERSION 0.1.0 LANGUAGES C CXX)
    enable_testing()
endif()
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

# Explicitly static for this initial ABI; shared-library discovery comes later.
add_library(emu51_peripheral_sdk STATIC src/peripheral.cpp src/memory.cpp src/decoder.cpp src/wired_bus.cpp src/parallel_dac.c
    src/i8255.c src/adc0804.c src/components.cpp src/displays.cpp src/displays_registry.cpp src/audio.cpp src/registry.cpp src/registry_builtins.cpp)
add_library(emu51::peripheral ALIAS emu51_peripheral_sdk)
set_target_properties(emu51_peripheral_sdk PROPERTIES
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED TRUE CXX_EXTENSIONS FALSE
    C_STANDARD 99 C_STANDARD_REQUIRED TRUE C_EXTENSIONS FALSE
    POSITION_INDEPENDENT_CODE TRUE EXPORT_NAME peripheral)
target_include_directories(emu51_peripheral_sdk PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")
if(TARGET emu51_warnings)
    target_link_libraries(emu51_peripheral_sdk PRIVATE "$<BUILD_INTERFACE:emu51_warnings>")
    if(EMU51_ASAN AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
        # Installed consumers of an instrumented static archive need its runtime.
        target_link_options(emu51_peripheral_sdk INTERFACE -fsanitize=address,undefined)
    endif()
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    target_compile_options(emu51_peripheral_sdk PRIVATE -Wall -Wextra -Wpedantic -Werror)
elseif(MSVC)
    target_compile_options(emu51_peripheral_sdk PRIVATE /W4 /WX)
endif()

option(EMU51_PERIPHERAL_HOST_AUDIO "Build the optional native peripheral audio backend" OFF)
if(EMU51_PERIPHERAL_HOST_AUDIO)
    set(EMU51_MINIAUDIO_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../third_party/miniaudio" CACHE PATH "Pinned miniaudio header and license directory")
    if(NOT EXISTS "${EMU51_MINIAUDIO_DIR}/miniaudio.h" OR NOT EXISTS "${EMU51_MINIAUDIO_DIR}/LICENSE")
        message(FATAL_ERROR "Native audio needs miniaudio 0.11.26. Set EMU51_MINIAUDIO_DIR or disable EMU51_PERIPHERAL_HOST_AUDIO.")
    endif()
    find_package(Threads REQUIRED)
    set(audio_defines MA_NO_DECODING MA_NO_ENCODING MA_NO_RESOURCE_MANAGER MA_NO_NODE_GRAPH MA_NO_ENGINE MA_NO_GENERATION)
    add_library(emu51_miniaudio OBJECT src/miniaudio_impl.c)
    set_target_properties(emu51_miniaudio PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED TRUE
        C_EXTENSIONS OFF POSITION_INDEPENDENT_CODE TRUE)
    target_compile_definitions(emu51_miniaudio PRIVATE ${audio_defines})
    target_include_directories(emu51_miniaudio SYSTEM PRIVATE "${EMU51_MINIAUDIO_DIR}")
    add_library(emu51_peripheral_audio_host STATIC src/audio_host.cpp $<TARGET_OBJECTS:emu51_miniaudio>)
    add_library(emu51::audio_host ALIAS emu51_peripheral_audio_host)
    set_target_properties(emu51_peripheral_audio_host PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED TRUE
        CXX_EXTENSIONS OFF POSITION_INDEPENDENT_CODE TRUE EXPORT_NAME audio_host)
    target_compile_definitions(emu51_peripheral_audio_host PRIVATE ${audio_defines})
    target_include_directories(emu51_peripheral_audio_host SYSTEM PRIVATE "${EMU51_MINIAUDIO_DIR}")
    target_link_libraries(emu51_peripheral_audio_host PUBLIC emu51_peripheral_sdk Threads::Threads ${CMAKE_DL_LIBS})
    if(UNIX)
        target_link_libraries(emu51_peripheral_audio_host PUBLIC m)
    endif()
    if(TARGET emu51_warnings)
        target_link_libraries(emu51_peripheral_audio_host PRIVATE "$<BUILD_INTERFACE:emu51_warnings>")
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
        target_compile_options(emu51_peripheral_audio_host PRIVATE -Wall -Wextra -Wpedantic -Werror)
    elseif(MSVC)
        target_compile_options(emu51_peripheral_audio_host PRIVATE /W4 /WX)
    endif()
    install(TARGETS emu51_peripheral_audio_host EXPORT emu51PeripheralsTargets
        ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
    install(FILES "${EMU51_MINIAUDIO_DIR}/LICENSE"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/doc/emu51-peripheral-sdk/licenses/miniaudio")
endif()

option(EMU51_PERIPHERAL_BUILD_TESTS "Build peripheral SDK tests and C wiring example" ON)
if(EMU51_PERIPHERAL_BUILD_TESTS)
    find_package(Threads REQUIRED)
    add_executable(emu51_memory_bus_tests tests/memory_bus_tests.cpp)
    target_link_libraries(emu51_memory_bus_tests PRIVATE emu51_peripheral_sdk)
    target_compile_features(emu51_memory_bus_tests PRIVATE cxx_std_17)
    if(TARGET emu51_warnings)
        target_link_libraries(emu51_memory_bus_tests PRIVATE emu51_warnings)
    endif()
    add_test(NAME emu51_memory_bus_tests COMMAND emu51_memory_bus_tests)
    add_executable(emu51_peripheral_display_tests tests/display_tests.cpp)
    target_compile_features(emu51_peripheral_display_tests PRIVATE cxx_std_17)
    target_link_libraries(emu51_peripheral_display_tests PRIVATE emu51_peripheral_sdk)
    if(TARGET emu51_warnings)
        target_link_libraries(emu51_peripheral_display_tests PRIVATE emu51_warnings)
    endif()
    add_test(NAME emu51_peripheral_display_tests COMMAND emu51_peripheral_display_tests)
    add_executable(emu51_peripheral_component_tests tests/component_tests.cpp)
    target_compile_features(emu51_peripheral_component_tests PRIVATE cxx_std_17)
    target_link_libraries(emu51_peripheral_component_tests PRIVATE emu51_peripheral_sdk)
    if(TARGET emu51_warnings)
        target_link_libraries(emu51_peripheral_component_tests PRIVATE emu51_warnings)
    endif()
    add_test(NAME emu51_peripheral_component_tests COMMAND emu51_peripheral_component_tests)
    add_executable(emu51_peripheral_audio_tests tests/audio_tests.cpp)
    target_compile_features(emu51_peripheral_audio_tests PRIVATE cxx_std_17)
    target_link_libraries(emu51_peripheral_audio_tests PRIVATE emu51_peripheral_sdk Threads::Threads)
    add_test(NAME emu51_peripheral_audio_tests COMMAND emu51_peripheral_audio_tests)
    if(EMU51_PERIPHERAL_HOST_AUDIO)
        add_executable(emu51_peripheral_audio_host_tests tests/audio_host_tests.cpp)
        target_link_libraries(emu51_peripheral_audio_host_tests PRIVATE emu51_peripheral_audio_host)
        add_test(NAME emu51_peripheral_audio_host_tests COMMAND emu51_peripheral_audio_host_tests)
        add_executable(emu51_peripheral_audio_demo examples/audio_host.cpp)
        target_link_libraries(emu51_peripheral_audio_demo PRIVATE emu51_peripheral_audio_host)
        if(APPLE)
            target_link_options(emu51_peripheral_audio_demo PRIVATE
                "LINKER:-sectcreate,__TEXT,__info_plist,${CMAKE_CURRENT_SOURCE_DIR}/examples/AudioDemoInfo.plist")
            set_property(TARGET emu51_peripheral_audio_demo APPEND PROPERTY LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/examples/AudioDemoInfo.plist")
        endif()
        add_test(NAME emu51_peripheral_audio_null
            COMMAND emu51_peripheral_audio_demo --null --capture default --playback default --seconds 0.15)
        if(TARGET emu51_warnings)
            target_link_libraries(emu51_peripheral_audio_host_tests PRIVATE emu51_warnings)
            target_link_libraries(emu51_peripheral_audio_demo PRIVATE emu51_warnings)
        endif()
    endif()
    add_executable(emu51_peripheral_tests tests/runtime_tests.cpp)
    target_compile_features(emu51_peripheral_tests PRIVATE cxx_std_17)
    target_link_libraries(emu51_peripheral_tests PRIVATE emu51_peripheral_sdk)
    add_test(NAME emu51_peripheral_tests COMMAND emu51_peripheral_tests)
    add_executable(emu51_peripheral_device_tests tests/device_tests.cpp)
    target_compile_features(emu51_peripheral_device_tests PRIVATE cxx_std_17)
    target_link_libraries(emu51_peripheral_device_tests PRIVATE emu51_peripheral_sdk)
    add_test(NAME emu51_peripheral_device_tests COMMAND emu51_peripheral_device_tests)
    add_executable(emu51_peripheral_registry_tests tests/registry_tests.cpp)
    target_compile_features(emu51_peripheral_registry_tests PRIVATE cxx_std_17)
    target_link_libraries(emu51_peripheral_registry_tests PRIVATE emu51_peripheral_sdk)
    add_test(NAME emu51_peripheral_registry_tests COMMAND emu51_peripheral_registry_tests)
    add_executable(emu51_peripheral_registry_demo examples/registry.c)
    set_target_properties(emu51_peripheral_registry_demo PROPERTIES
        C_STANDARD 99 C_STANDARD_REQUIRED TRUE C_EXTENSIONS FALSE)
    target_link_libraries(emu51_peripheral_registry_demo PRIVATE emu51_peripheral_sdk)
    add_test(NAME emu51_peripheral_registry_demo COMMAND emu51_peripheral_registry_demo)
    add_executable(emu51_peripheral_wiring examples/wiring.c examples/parallel_expander.c)
    set_target_properties(emu51_peripheral_wiring PROPERTIES
        C_STANDARD 99 C_STANDARD_REQUIRED TRUE C_EXTENSIONS FALSE)
    target_link_libraries(emu51_peripheral_wiring PRIVATE emu51_peripheral_sdk)
    add_test(NAME emu51_peripheral_wiring COMMAND emu51_peripheral_wiring)
    add_test(NAME emu51_peripheral_sdk_consumer
        COMMAND "${CMAKE_COMMAND}"
            "-DSDK_BUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}"
            "-DCONFIG=$<CONFIG>"
            "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/consumer-test/$<CONFIG>"
            "-DGENERATOR=${CMAKE_GENERATOR}"
            "-DPLATFORM=${CMAKE_GENERATOR_PLATFORM}"
            "-DTOOLSET=${CMAKE_GENERATOR_TOOLSET}"
            "-DC_COMPILER=${CMAKE_C_COMPILER}"
            "-DCXX_COMPILER=${CMAKE_CXX_COMPILER}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/installed_consumer.cmake")
    if(TARGET emu51_warnings)
        target_link_libraries(emu51_peripheral_audio_tests PRIVATE emu51_warnings)
        target_link_libraries(emu51_peripheral_tests PRIVATE emu51_warnings)
        target_link_libraries(emu51_peripheral_device_tests PRIVATE emu51_warnings)
        target_link_libraries(emu51_peripheral_registry_tests PRIVATE emu51_warnings)
        target_link_libraries(emu51_peripheral_registry_demo PRIVATE emu51_warnings)
        target_link_libraries(emu51_peripheral_wiring PRIVATE emu51_warnings)
    endif()
endif()

configure_package_config_file(cmake/emu51PeripheralsConfig.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/emu51PeripheralsConfig.cmake"
    INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/emu51Peripherals")
write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/emu51PeripheralsConfigVersion.cmake"
    VERSION 0.1.0 COMPATIBILITY ExactVersion)
install(TARGETS emu51_peripheral_sdk EXPORT emu51PeripheralsTargets
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
install(FILES include/emu51/peripheral.h include/emu51/memory.h include/emu51/decoder.h include/emu51/snapshot.h include/emu51/wired_bus.h include/emu51/devices.h include/emu51/components.h include/emu51/displays.h include/emu51/audio.h include/emu51/audio_host.h include/emu51/registry.h
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/emu51")
install(EXPORT emu51PeripheralsTargets NAMESPACE emu51::
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/emu51Peripherals")
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/emu51PeripheralsConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/emu51PeripheralsConfigVersion.cmake"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/emu51Peripherals")
install(FILES LICENSE COPYING README.md AUDIO.md MEMORY_BUS.md REGISTRY.md COMPONENTS.md DISPLAYS.md DESTINATION "${CMAKE_INSTALL_DATADIR}/doc/emu51-peripheral-sdk")
