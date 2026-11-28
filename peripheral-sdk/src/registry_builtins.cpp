// SPDX-License-Identifier: LGPL-2.1-only
// Copyright (C) 2026 Mariusz Kasolik
#include "registry_internal.h"
#include "emu51/serial_devices.h"
#include "emu51/devices.h"
#include "emu51/components.h"
#include "emu51/displays.h"
#include <array>
#include <cstring>
#include <limits>

namespace {
e51p_parameter_desc real(const char* name, const char* label, double value) {
    e51p_parameter_desc p{}; p.struct_size = sizeof(p); p.name = name; p.label = label;
    p.type = E51P_PARAMETER_REAL; p.real_default = value;
    p.real_min = std::numeric_limits<double>::denorm_min(); p.real_max = std::numeric_limits<double>::max();
    return p;
}
e51p_parameter_desc ticks() {
    e51p_parameter_desc p{}; p.struct_size = sizeof(p); p.name = "conversion_ticks";
    p.label = "Conversion time (oscillator ticks)"; p.type = E51P_PARAMETER_INTEGER;
    p.integer_default = 768; p.integer_min = 1; p.integer_max = INT64_MAX; return p;
}
e51p_status ppi(void*, const e51p_api* api, e51p_context* c, const char* id,
    const e51p_parameter_value*, uint32_t count, e51p_endpoint* out) {
    if (count || !out) return E51P_INVALID_ARGUMENT;
    e51p_i8255_endpoints p{}; p.struct_size = sizeof(p);
    const auto s = e51p_add_i8255_mode0(api, c, id, &p);
    if (s == E51P_OK) { out[0] = p.bus; out[1] = p.port_a; out[2] = p.port_b; out[3] = p.port_c; out[4] = p.reset; }
    return s;
}
e51p_status adc(void*, const e51p_api* api, e51p_context* c, const char* id,
    const e51p_parameter_value* v, uint32_t count, e51p_endpoint* out) {
    if (count != 2 || !v || !out) return E51P_INVALID_ARGUMENT;
    e51p_adc0804_endpoints p{}; p.struct_size = sizeof(p);
    const auto s = e51p_add_adc0804(api, c, id, static_cast<uint64_t>(v[0].integer), v[1].real, &p);
    if (s == E51P_OK) {
        out[0] = p.data; out[1] = p.cs_n; out[2] = p.rd_n; out[3] = p.wr_n;
        out[4] = p.intr_n; out[5] = p.vin_positive; out[6] = p.vin_negative;
    }
    return s;
}
e51p_status dac(void*, const e51p_api* api, e51p_context* c, const char* id,
    const e51p_parameter_value* v, uint32_t count, e51p_endpoint* out) {
    if (count != 1 || !v || !out) return E51P_INVALID_ARGUMENT;
    return e51p_add_parallel_dac(api, c, id, v[0].real, &out[0], &out[1]);
}
e51p_parameter_desc integer(const char* name, const char* label, int64_t value, int64_t minimum, int64_t maximum) {
    e51p_parameter_desc p{}; p.struct_size = sizeof(p); p.name = name; p.label = label;
    p.type = E51P_PARAMETER_INTEGER; p.integer_default = value; p.integer_min = minimum; p.integer_max = maximum; return p;
}
e51p_status component(void* user, const e51p_api* api, e51p_context* c, const char* id,
    const e51p_parameter_value* values, uint32_t count, e51p_endpoint* out) {
    auto cfg = e51p_component_config_init(*static_cast<const uint32_t*>(user));
    uint64_t pulseTicks = 0, conversionTicks = 1200;
    double fullScale = 5;
    uint32_t directKeys = 0;
    for (uint32_t i = 0; i < count; ++i) {
        const auto& v = values[i];
        if (!std::strcmp(v.name,"active_low")) cfg.active_low = static_cast<int>(v.integer);
        else if (!std::strcmp(v.name,"initially_pressed")) cfg.initially_pressed = static_cast<int>(v.integer);
        else if (!std::strcmp(v.name,"bounce_ticks")) cfg.bounce_ticks = static_cast<uint64_t>(v.integer);
        else if (!std::strcmp(v.name,"columns")) cfg.columns = static_cast<uint32_t>(v.integer);
        else if (!std::strcmp(v.name,"rows")) cfg.rows = static_cast<uint32_t>(v.integer);
        else if (!std::strcmp(v.name,"i2c_address")) cfg.i2c_address = static_cast<uint32_t>(v.integer);
        else if (!std::strcmp(v.name,"clock_hz")) cfg.clock_hz = v.real;
        else if (!std::strcmp(v.name,"pulse_ticks")) pulseTicks = static_cast<uint64_t>(v.integer);
        else if (!std::strcmp(v.name,"direct_keys")) directKeys = static_cast<uint32_t>(v.integer);
        else if (!std::strcmp(v.name,"conversion_ticks")) conversionTicks = static_cast<uint64_t>(v.integer);
        else if (!std::strcmp(v.name,"full_scale_volts")) fullScale = v.real;
    }
    e51p_component* handle = nullptr;
    uint32_t pinCount = 0; (void)e51p_component_pins(cfg.kind,&pinCount);
    const auto status = e51p_add_component(api,c,id,&cfg,&handle,out,pinCount);
    if (status != E51P_OK) return status;
    if (cfg.kind == E51P_COMPONENT_BUS_LATCH) return e51p_component_set_latch_pulse_ticks(handle,pulseTicks);
    if (cfg.kind == E51P_COMPONENT_MATRIX) return e51p_component_set_matrix_direct_keys(handle,directKeys);
    if (cfg.kind == E51P_COMPONENT_ADC0809) return e51p_component_set_adc0809(handle,conversionTicks,fullScale);
    return status;
}

}
static e51p_status serialDevice(void* user,const e51p_api* api,e51p_context* c,const char* id,
    const e51p_parameter_value* values,uint32_t count,e51p_endpoint* out) {
    auto config = e51p_serial_config_init(*static_cast<uint32_t*>(user));
    for (uint32_t i = 0; i < count; ++i) {
        const auto& value = values[i];
        if (!std::strcmp(value.name,"i2c_address")) config.address = static_cast<uint32_t>(value.integer);
        else if (!std::strcmp(value.name,"rtc_day")) config.rtc_day = static_cast<uint32_t>(value.integer);
        else if (!std::strcmp(value.name,"rtc_second")) config.rtc_second = static_cast<uint32_t>(value.integer);
    }
    uint32_t pins = 0;
    e51p_serial_pins(config.kind,&pins);
    e51p_serial_device* device = nullptr;
    return e51p_add_serial_device(api,c,id,&config,&device,out,pins);
}
const e51p_factory_desc* const* e51p_builtin_factories(std::size_t& count) {
    static const e51p_parameter_desc adcParameters[] = {ticks(), real("full_scale_volts", "ADC full scale (V)", 5.0)};
    static const e51p_parameter_desc dacParameters[] = {real("reference_volts", "DAC reference (V)", 5.0)};
    static const e51p_endpoint_desc ppiPins[] = {
        {sizeof(e51p_endpoint_desc), "bus", E51P_BUS_TARGET, 0, 8, 2, 0, 0},
        {sizeof(e51p_endpoint_desc), "A", E51P_DIGITAL, E51P_INPUT | E51P_OUTPUT, 8, 0, 0, 0},
        {sizeof(e51p_endpoint_desc), "B", E51P_DIGITAL, E51P_INPUT | E51P_OUTPUT, 8, 0, 0, 0},
        {sizeof(e51p_endpoint_desc), "C", E51P_DIGITAL, E51P_INPUT | E51P_OUTPUT, 8, 0, 0, 0},
        {sizeof(e51p_endpoint_desc), "RESET", E51P_DIGITAL, E51P_INPUT, 1, 0, 0, 1},
    };
    static const e51p_endpoint_desc adcPins[] = {
        {sizeof(e51p_endpoint_desc), "data", E51P_DIGITAL, E51P_OUTPUT, 8, 0, 0, 0},
        {sizeof(e51p_endpoint_desc), "CS", E51P_DIGITAL, E51P_INPUT, 1, 0, 1, 0},
        {sizeof(e51p_endpoint_desc), "RD", E51P_DIGITAL, E51P_INPUT, 1, 0, 1, 0},
        {sizeof(e51p_endpoint_desc), "WR", E51P_DIGITAL, E51P_INPUT, 1, 0, 1, 0},
        {sizeof(e51p_endpoint_desc), "INTR", E51P_DIGITAL, E51P_OUTPUT, 1, 0, 0, 0},
        {sizeof(e51p_endpoint_desc), "plus", E51P_ANALOG, E51P_INPUT, 1, 0, 0, 0},
        {sizeof(e51p_endpoint_desc), "minus", E51P_ANALOG, E51P_INPUT, 1, 0, 0, 0},
    };
    static const e51p_endpoint_desc dacPins[] = {
        {sizeof(e51p_endpoint_desc), "data", E51P_DIGITAL, E51P_INPUT, 8, 0, 0, 0},
        {sizeof(e51p_endpoint_desc), "out", E51P_ANALOG, E51P_OUTPUT, 1, 0, 0, 0},
    };
    static const e51p_factory_desc ppiFactory{sizeof(e51p_factory_desc), "emu51.i8255.mode0", "8255 (mode 0 / BSR)",
        nullptr, 0, ppiPins, 5, nullptr, ppi};
    static const e51p_factory_desc adcFactory{sizeof(e51p_factory_desc), "emu51.adc0804", "ADC0804",
        adcParameters, 2, adcPins, 7, nullptr, adc};
    static const e51p_factory_desc dacFactory{sizeof(e51p_factory_desc), "emu51.parallel-dac", "Parallel DAC / Covox",
        dacParameters, 1, dacPins, 2, nullptr, dac};
    static std::array<uint32_t,E51P_COMPONENT_COUNT> kinds = [] {
        std::array<uint32_t,E51P_COMPONENT_COUNT> ids{};
        for (uint32_t i=0; i<ids.size(); ++i) ids[i]=i;
        return ids;
    }();
    static const e51p_parameter_desc ledParameters[]{integer("active_low","Active low",1,0,1)};
    static const e51p_parameter_desc switchParameters[]{integer("active_low","Active low",1,0,1),
        integer("initially_pressed","Initially closed",0,0,1)};
    static const e51p_parameter_desc buttonParameters[]{integer("active_low","Active low",1,0,1),
        integer("bounce_ticks","Contact bounce (ticks)",0,0,INT64_MAX)};
    static const e51p_parameter_desc lcdParameters[]{integer("columns","Columns",16,1,20),integer("rows","Rows (1, 2 or 4)",2,1,4)};
    static const e51p_parameter_desc eepromParameters[]{integer("i2c_address","I2C address",0x50,0x50,0x57)};
    static const e51p_parameter_desc ayParameters[]{[] { auto p = real("clock_hz","Chip clock (Hz)",2000000); p.real_min=1000; p.real_max=4000000; return p; }()};
    static const e51p_parameter_desc matrixParameters[]{integer("columns","Columns",2,1,4),integer("rows","Rows",8,1,8),integer("direct_keys","Independent keys",0,0,8)};
    static const e51p_parameter_desc pulseParameters[]{integer("pulse_ticks","Retriggerable pulse duration (ticks)",0,0,INT64_MAX)};
    static const e51p_parameter_desc adc0809Parameters[]{
        integer("conversion_ticks","Conversion time (oscillator ticks)",1200,1,INT64_MAX), real("full_scale_volts","ADC full scale (V)",5.0)};
    static const auto components = [] {
        std::array<e51p_factory_desc,E51P_COMPONENT_COUNT> result{};
        const char* ids[]{"emu51.led","emu51.switch","emu51.button","emu51.hd44780","emu51.keypad4x4",
            "emu51.74hc595","emu51.74hc165","emu51.24c02","emu51.ay38910",
            "emu51.bus-latch8","emu51.bus-buffer8","emu51.key-matrix","emu51.24lc256","emu51.uln2003","emu51.stepper","emu51.adc0809"};
        const char* labels[]{"LED","Switch","Pushbutton","HD44780 LCD","4x4 matrix keypad","74HC595","74HC165","24C02 EEPROM","AY-3-8910",
            "8-bit bus output latch","8-bit bus input buffer","Configurable key matrix","24LC256 EEPROM","ULN2003","Four-phase stepper","ADC0809"};
        const e51p_parameter_desc* parameters[]{ledParameters,switchParameters,buttonParameters,lcdParameters,nullptr,nullptr,nullptr,eepromParameters,ayParameters,pulseParameters,nullptr,matrixParameters,eepromParameters,nullptr,nullptr,adc0809Parameters};
        const uint32_t counts[]{1,2,2,2,0,0,0,1,1,1,0,3,1,0,0,2};
        for (uint32_t i=0; i<E51P_COMPONENT_COUNT; ++i) {
            uint32_t pinCount=0; const auto* pins=e51p_component_pins(i,&pinCount);
            result[i]={sizeof(e51p_factory_desc),ids[i],labels[i],parameters[i],counts[i],pins,pinCount,&kinds[i],component};
        }
        return result;
    }();
    static const auto serial = [] {
        static uint32_t devices[]{E51P_SSD1306,E51P_DS1307,E51P_MCP23017,E51P_W25Q64,E51P_LOGIC_ANALYZER};
        static const auto oled = integer("i2c_address","I2C address",0x3c,0x3c,0x3d);
        static const auto expander = integer("i2c_address","I2C address",0x20,0x20,0x27);
        static const e51p_parameter_desc rtc[]{integer("rtc_day","Days since 2000-01-01",0,0,36524),
            integer("rtc_second","Seconds since midnight",0,0,86399)};
        constexpr const char* labels[]{"SSD1306 OLED","DS1307 real-time clock","MCP23017 GPIO expander","W25Q64 SPI flash","Logic analyzer"};
        const e51p_parameter_desc* parameters[]{&oled,rtc,&expander,nullptr,nullptr};
        constexpr uint32_t counts[]{1,2,1,0,0};
        std::array<e51p_factory_desc,E51P_SERIAL_DEVICE_COUNT> result{};
        for (uint32_t i = 0; i < result.size(); ++i) {
            uint32_t count = 0;
            const auto* pins = e51p_serial_pins(i,&count);
            result[i] = {sizeof(e51p_factory_desc),e51p_serial_type_id(i),labels[i],parameters[i],counts[i],pins,count,&devices[i],serialDevice};
        }
        return result;
    }();
    static const auto factories = [] {
        std::array<const e51p_factory_desc*,3+E51P_COMPONENT_COUNT+E51P_DISPLAY_COUNT+E51P_SERIAL_DEVICE_COUNT> result{};
        result[0]=&ppiFactory; result[1]=&adcFactory; result[2]=&dacFactory;
        for (uint32_t i=0; i<E51P_COMPONENT_COUNT; ++i) result[i+3]=&components[i];
        std::size_t count=0; const auto* displays=e51p_display_factories(count);
        for (std::size_t i=0;i<count;++i) result[i+3+E51P_COMPONENT_COUNT]=displays[i];
        for (std::size_t i=0;i<serial.size();++i) result[i+3+E51P_COMPONENT_COUNT+E51P_DISPLAY_COUNT]=&serial[i];
        return result;
    }();
    count = factories.size(); return factories.data();
}
