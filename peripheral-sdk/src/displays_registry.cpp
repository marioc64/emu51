// SPDX-License-Identifier: LGPL-2.1-only
#include "registry_internal.h"
#include "emu51/displays.h"
#include <array>
#include <cstring>
namespace {
e51p_parameter_desc integer(const char* name,const char* label,int64_t value,int64_t low,int64_t high) {
    e51p_parameter_desc p{}; p.struct_size=sizeof(p); p.name=name; p.label=label; p.type=E51P_PARAMETER_INTEGER;
    p.integer_default=value; p.integer_min=low; p.integer_max=high; return p;
}
e51p_parameter_desc persistence() {
    e51p_parameter_desc p{}; p.struct_size=sizeof(p); p.name="persistence_ms"; p.label="Persistence (ms)"; p.type=E51P_PARAMETER_REAL;
    p.real_default=20; p.real_min=1; p.real_max=1000; return p;
}
e51p_status create(void* user,const e51p_api* api,e51p_context* c,const char* id,
    const e51p_parameter_value* values,uint32_t count,e51p_endpoint* out) {
    auto cfg=e51p_display_config_init(*static_cast<uint32_t*>(user));
    for (uint32_t i=0;i<count;++i) {
        const auto& v=values[i];
        if (!std::strcmp(v.name,"digits")) cfg.digits=static_cast<uint32_t>(v.integer);
        else if (!std::strcmp(v.name,"bar_segments")) cfg.bar_segments=static_cast<uint32_t>(v.integer);
        else if (!std::strcmp(v.name,"common_anode")) cfg.common_anode=static_cast<int>(v.integer);
        else if (!std::strcmp(v.name,"colon")) cfg.colon=static_cast<int>(v.integer);
        else if (!std::strcmp(v.name,"persistence_ms")) cfg.persistence_ms=v.real;
        else if (!std::strcmp(v.name,"i2c_address")) cfg.i2c_address=static_cast<uint32_t>(v.integer);
    }
    e51p_display* display=nullptr; uint32_t pins=0; e51p_display_pins(cfg.kind,&pins);
    return e51p_add_display(api,c,id,&cfg,&display,out,pins);
}
}
const e51p_factory_desc* const* e51p_display_factories(std::size_t& count) {
    static uint32_t kinds[]{0,1,2,3,4,5,6,7,8,9};
    static const e51p_parameter_desc raw[]{integer("digits","Digits",4,1,8),integer("common_anode","Common anode",0,0,1),integer("colon","Colon",0,0,1),persistence()};
    static const e51p_parameter_desc bar[]{integer("bar_segments","Segments",10,1,16),integer("common_anode","Active low",0,0,1),persistence()};
    static const e51p_parameter_desc decoder[]{persistence()};
    static const e51p_parameter_desc max[]{integer("digits","Preview digits",8,1,8),integer("colon","Colon from DP on digit 1",0,0,1)};
    static const e51p_parameter_desc ht[]{integer("digits","Preview digits",8,1,8),integer("i2c_address","I2C address",0x70,0x70,0x77),integer("colon","Colon from bit 15 on digit 1",0,0,1)};
    static const e51p_parameter_desc tm[]{integer("digits","Preview digits",4,1,6),integer("colon","Colon from DP on digit 1",0,0,1)};
    static const auto descriptors=[] {
        std::array<e51p_factory_desc,E51P_DISPLAY_COUNT> result{};
        const e51p_parameter_desc* parameters[]{raw,raw,raw,bar,decoder,decoder,max,max,ht,tm};
        const uint32_t counts[]{4,4,4,3,1,1,2,2,3,2};
        for (uint32_t i=0;i<E51P_DISPLAY_COUNT;++i) {
            uint32_t pins=0; const auto* p=e51p_display_pins(i,&pins);
            result[i]={sizeof(e51p_factory_desc),e51p_display_type_id(i),e51p_display_name(i),parameters[i],counts[i],p,pins,&kinds[i],create};
        }
        return result;
    }();
    static const auto pointers=[] { std::array<const e51p_factory_desc*,E51P_DISPLAY_COUNT> result{};
        for (uint32_t i=0;i<E51P_DISPLAY_COUNT;++i) result[i]=&descriptors[i]; return result; }();
    count=pointers.size(); return pointers.data();
}
