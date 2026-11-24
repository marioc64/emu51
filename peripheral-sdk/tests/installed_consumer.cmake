# SPDX-License-Identifier: LGPL-2.1-only
# Install only the SDK subdirectory, move it, then compile a separate C client.
file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}/consumer")
execute_process(COMMAND "${CMAKE_COMMAND}" --install "${SDK_BUILD_DIR}"
    --config "${CONFIG}" --prefix "${WORK_DIR}/sdk original"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "SDK install failed: ${output}\n${error}")
endif()
file(RENAME "${WORK_DIR}/sdk original" "${WORK_DIR}/sdk relocated")
file(WRITE "${WORK_DIR}/consumer/CMakeLists.txt" [=[
cmake_minimum_required(VERSION 3.16)
project(peripheral_sdk_consumer LANGUAGES C CXX)
enable_testing()
find_package(emu51Peripherals 0.1.0 EXACT CONFIG REQUIRED)
add_executable(consumer main.c)
set_target_properties(consumer PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED TRUE C_EXTENSIONS FALSE)
target_link_libraries(consumer PRIVATE emu51::peripheral)
if(TARGET emu51::audio_host)
    target_link_libraries(consumer PRIVATE emu51::audio_host)
    target_compile_definitions(consumer PRIVATE HAVE_AUDIO_HOST)
endif()
add_test(NAME consumer COMMAND consumer)
]=])
file(WRITE "${WORK_DIR}/consumer/main.c" [=[
#include <emu51/peripheral.h>
#include <emu51/devices.h>
#include <emu51/components.h>
#include <emu51/displays.h>
#include <emu51/registry.h>
#include <emu51/audio.h>
#include <emu51/wired_bus.h>
#ifdef HAVE_AUDIO_HOST
#include <emu51/audio_host.h>
#endif
int main(void) {
    const e51p_api* api = 0;
    e51p_context* context = 0;
    uint64_t time = 0, hz = 0;
    e51p_endpoint data, voltage;
    e51p_endpoint dac_pins[2];
    e51p_registry* registry = 0;
    const e51p_factory_desc* factory = 0;
    e51p_i8255_endpoints ppi = {0};
    e51p_adc0804_endpoints adc = {0};
    e51p_audio_config audio_config = e51p_audio_config_init(E51P_AUDIO_CAPTURE | E51P_AUDIO_PLAYBACK);
    e51p_audio_endpoints audio_pins = {0};
    e51p_audio_stream* audio = 0;
    e51p_component* button = 0;
    e51p_component* led = 0;
    e51p_endpoint button_pins[1], led_pins[1];
    e51p_display* segments = 0;
    e51p_endpoint display_pins[3];
    e51p_display_config display_config = e51p_display_config_init(E51P_DISPLAY_7SEG);
    e51p_component_config component = e51p_component_config_init(E51P_COMPONENT_BUTTON);
    if (e51p_get_api(E51P_ABI_VERSION, &api) != E51P_OK) return 1;
    if (api->create(12000000, &context) != E51P_OK) return 2;
    {
        e51p_component* latch = 0;
        e51p_endpoint pins[5];
        e51p_component_config config = e51p_component_config_init(E51P_COMPONENT_BUS_LATCH);
        if (e51p_add_component(api, context, "bus-latch", &config, &latch, pins, 5) != E51P_OK ||
            e51p_component_set_latch_pulse_ticks(latch, 300000) != E51P_OK) return 33;
        config = e51p_component_config_init(E51P_COMPONENT_MATRIX);
        if (e51p_add_component(api, context, "key-matrix", &config, &latch, pins, 5) != E51P_OK ||
            e51p_component_set_matrix_direct_keys(latch, 2) != E51P_OK) return 34;
    }
    {
        e51p_memory* memory = 0;
        const e51p_memory_config config = {sizeof(config), E51P_ROM, 131072, 255};
        const uint8_t bytes[] = {0x74, 0x42};
        uint8_t out[2] = {0};
        e51p_decoder* decoder = 0;
        const uint32_t values[] = {0, 3};
        const e51p_decoder_output output = {sizeof(output), "SELECT", 1, values, 2};
        uint32_t selected = E51P_INVALID_ID;
        e51p_wired_bus* bus = 0;
        e51p_initiator_pins pins = {0};
        e51p_bus_pins target_pins = {0};
        uint32_t target = 0;
        if (e51p_memory_create(&config, &memory) != E51P_OK ||
            e51p_memory_program(memory, 65536, bytes, 2) != E51P_OK ||
            e51p_memory_copy(memory, 65536, out, 2) != E51P_OK || out[1] != 0x42 ||
            e51p_memory_write(memory, 65536, 0) != E51P_UNSUPPORTED ||
            e51p_memory_page_revision(memory, 65536) == 0) return 30;
        if (e51p_decoder_create(2, &output, 1, &decoder) != E51P_OK ||
            e51p_decoder_select(decoder, (e51p_digital_state){3,0,0}, &selected) != E51P_OK || selected != 0) return 31;
        if (e51p_wired_bus_create(api, context, "memory-bus", 16, &bus, &pins) != E51P_OK ||
            e51p_wired_bus_add_memory(bus, "rom", memory, 1, 1, 1, &target_pins, &target) != E51P_OK) return 32;
        e51p_wired_bus_destroy(bus);
        e51p_decoder_destroy(decoder);
        e51p_memory_release(memory);
    }
    if (e51p_registry_create(E51P_REGISTRY_ABI_VERSION, &registry) != E51P_OK) return 11;
    if (e51p_registry_add_builtins(registry) != E51P_OK) return 12;
    if (e51p_registry_find(registry, "emu51.parallel-dac", &factory) != E51P_OK || factory->pin_count != 2) return 13;
    if (e51p_registry_instantiate(registry, factory->type_id, api, context, "dac", 0, 0, dac_pins, 2) != E51P_OK) return 5;
    data = dac_pins[0]; voltage = dac_pins[1];
    if (data == E51P_INVALID_ID || voltage == E51P_INVALID_ID) return 14;
    e51p_registry_destroy(registry);
    ppi.struct_size = sizeof(ppi);
    adc.struct_size = sizeof(adc);
    if (e51p_add_i8255_mode0(api, context, "ppi", &ppi) != E51P_OK) return 6;
    if (e51p_add_adc0804(api, context, "adc", 120, 5.0, &adc) != E51P_OK) return 7;
    audio_pins.struct_size = sizeof(audio_pins);
    if (e51p_audio_create(api, context, "audio", &audio_config, &audio, &audio_pins) != E51P_OK) return 8;
    if (e51p_add_component(api, context, "button", &component, &button, button_pins, 1) != E51P_OK) return 23;
    component = e51p_component_config_init(E51P_COMPONENT_LED);
    if (e51p_add_component(api, context, "led", &component, &led, led_pins, 1) != E51P_OK) return 24;
    if (api->connect_digital(context, button_pins[0], 0, led_pins[0], 0, 1) != E51P_OK) return 25;
    if (e51p_add_display(api, context, "segments", &display_config, &segments, display_pins, 3) != E51P_OK) return 27;
    if (api->start(context) != E51P_OK || api->advance_to(context, 12) != E51P_OK) return 3;
    {
        int on = 0, defined = 0;
        if (e51p_component_set_pressed(button, 1) != E51P_OK ||
            e51p_component_get_led(led, &on, &defined) != E51P_OK || !on || !defined) return 26;
    }
    if (api->clock(context, &time, &hz) != E51P_OK) return 4;
    {
        e51p_segment_snapshot frame = {0}; frame.struct_size = sizeof(frame);
        if (e51p_display_snapshot(segments, &frame) != E51P_OK || frame.digits != 4 || frame.segments != 8) return 28;
    }
#ifdef HAVE_AUDIO_HOST
    {
        e51p_audio_host* host = 0;
        uint32_t count = 0;
        if (e51p_audio_host_create(E51P_AUDIO_NULL, &host) != E51P_OK) return 9;
        if (e51p_audio_host_device_count(host, E51P_AUDIO_PLAYBACK, &count) != E51P_OK || !count) return 10;
        e51p_audio_host_destroy(host);
    }
#endif
    if (e51p_audio_set_input_gain(audio, 10) != E51P_OK) return 20;
    {
        e51p_audio_scope_frame frame;
        uint32_t count = 0, rate = 0;
        if (e51p_audio_read_scope(audio, &frame, 1, &count, &rate) != E51P_OK) return 21;
    }
    if (e51p_audio_set_paused(audio, 1) != E51P_OK ||
        e51p_audio_set_buffering(audio, 8192, 1024) != E51P_OK) return 22;
    api->destroy(context);
    e51p_audio_release(audio);
    return time != 12 || hz != 12000000;
}
]=])
set(generator_options -G "${GENERATOR}")
if(PLATFORM)
    list(APPEND generator_options -A "${PLATFORM}")
endif()
if(TOOLSET)
    list(APPEND generator_options -T "${TOOLSET}")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" -S "${WORK_DIR}/consumer" -B "${WORK_DIR}/build"
    ${generator_options} "-DCMAKE_BUILD_TYPE=${CONFIG}"
    "-DCMAKE_C_COMPILER=${C_COMPILER}" "-DCMAKE_CXX_COMPILER=${CXX_COMPILER}"
    "-DCMAKE_PREFIX_PATH=${WORK_DIR}/sdk relocated"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Relocated SDK configure failed: ${output}\n${error}")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" --build "${WORK_DIR}/build" --config "${CONFIG}"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Relocated SDK build failed: ${output}\n${error}")
endif()
execute_process(COMMAND "${CMAKE_CTEST_COMMAND}" -C "${CONFIG}" --output-on-failure
    WORKING_DIRECTORY "${WORK_DIR}/build"
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Relocated SDK consumer failed: ${output}\n${error}")
endif()
message(STATUS "Relocated standalone C consumer passed")
