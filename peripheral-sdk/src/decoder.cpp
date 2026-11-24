// SPDX-License-Identifier: LGPL-2.1-only
// Copyright (C) 2026 Mariusz Kasolik
#include "emu51/decoder.h"
#include <memory>
#include <algorithm>
#include <new>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <atomic>

struct e51p_decoder {
    std::atomic<unsigned> references{1};
    uint32_t bits;
    struct Output { std::string id; bool active_low; };
    std::vector<Output> outputs;
    std::unordered_map<uint32_t, uint32_t> table;
};
namespace {
struct AttachedDecoder {
    const e51p_api* api;
    e51p_decoder* model;
    e51p_endpoint input = E51P_INVALID_ID;
    std::vector<e51p_endpoint> outputs;
    ~AttachedDecoder() { e51p_decoder_destroy(model); }
    static void reset(void* user, e51p_context* context) {
        auto& d = *static_cast<AttachedDecoder*>(user);
        for (uint32_t i = 0; i < d.outputs.size(); ++i)
            (void)d.api->drive(context,d.outputs[i],d.model->outputs[i].active_low,1,0);
    }
    static void changed(void* user, e51p_context* context, e51p_endpoint endpoint, e51p_digital_state input, uint64_t) {
        auto& d = *static_cast<AttachedDecoder*>(user);
        if (endpoint != d.input) return;
        for (uint32_t i = 0; i < d.outputs.size(); ++i) {
            e51p_digital_state level{};
            const auto status = e51p_decoder_level(d.model,i,input,&level);
            (void)d.api->drive(context,d.outputs[i],level.value,status == E51P_OK ? 1 : 0,0);
        }
    }
};
}
extern "C" {
e51p_status e51p_decoder_create(uint32_t bits, const e51p_decoder_output* outputs,
    uint32_t count, e51p_decoder** result) {
    if (!result) return E51P_INVALID_ARGUMENT;
    *result = nullptr;
    if (!bits || bits > 32 || (!outputs && count)) return E51P_INVALID_ARGUMENT;
    try {
        auto d = std::make_unique<e51p_decoder>();
        d->bits = bits;
        std::unordered_set<std::string> ids;
        for (uint32_t i = 0; i < count; ++i) {
            const auto& o = outputs[i];
            if (o.struct_size < sizeof(o) || !o.id || !*o.id || o.active_low > 1 ||
                (!o.values && o.value_count)) return E51P_INVALID_ARGUMENT;
            if (!ids.insert(o.id).second) return E51P_CONFLICT;
            d->outputs.push_back({o.id, o.active_low != 0});
            for (uint32_t j = 0; j < o.value_count; ++j) {
                if (bits < 32 && o.values[j] >= (UINT32_C(1) << bits)) return E51P_INVALID_ARGUMENT;
                if (!d->table.emplace(o.values[j], i).second) return E51P_CONFLICT;
            }
        }
        *result = d.release();
        return E51P_OK;
    } catch (const std::bad_alloc&) { return E51P_NO_MEMORY; }
    catch (...) { return E51P_DEVICE_ERROR; }
}
void e51p_decoder_destroy(e51p_decoder* d) { if (d && --d->references == 0) delete d; }
e51p_status e51p_decoder_attach(const e51p_api* api, e51p_context* context, const char* id,
    e51p_decoder* model, e51p_endpoint* input, e51p_endpoint* outputs, uint32_t capacity) {
    if (!api || !context || !id || !*id || !model || !input || capacity < model->outputs.size() || (!outputs && !model->outputs.empty()))
        return E51P_INVALID_ARGUMENT;
    try {
        auto state = std::make_unique<AttachedDecoder>(); state->api = api; state->model = model; ++model->references;
        state->outputs.resize(model->outputs.size());
        e51p_device_desc device{}; device.struct_size = sizeof(device); device.instance_id = id; device.user = state.get();
        device.destroy = [](void* user) { delete static_cast<AttachedDecoder*>(user); };
        device.reset = AttachedDecoder::reset; device.digital_changed = AttachedDecoder::changed;
        e51p_device handle;
        auto status = api->add_device(context,&device,&handle);
        if (status != E51P_OK) return status;
        auto* owned = state.release();
        const e51p_endpoint_desc in{sizeof(in),"IN",E51P_DIGITAL,E51P_INPUT,model->bits,0,0,0};
        status = api->add_endpoint(context,handle,&in,&owned->input);
        if (status != E51P_OK) return status;
        for (uint32_t i = 0; i < model->outputs.size(); ++i) {
            const e51p_endpoint_desc out{sizeof(out),model->outputs[i].id.c_str(),E51P_DIGITAL,E51P_OUTPUT,1,0,0,0};
            status = api->add_endpoint(context,handle,&out,&owned->outputs[i]);
            if (status != E51P_OK) return status;
        }
        *input = owned->input;
        if (!owned->outputs.empty()) std::copy(owned->outputs.begin(),owned->outputs.end(),outputs);
        return E51P_OK;
    } catch (const std::bad_alloc&) { return E51P_NO_MEMORY; }
    catch (...) { return E51P_DEVICE_ERROR; }
}
uint32_t e51p_decoder_input_bits(const e51p_decoder* d) { return d ? d->bits : 0; }
uint32_t e51p_decoder_output_count(const e51p_decoder* d) { return d ? static_cast<uint32_t>(d->outputs.size()) : 0; }
const char* e51p_decoder_output_id(const e51p_decoder* d, uint32_t output) {
    return d && output < d->outputs.size() ? d->outputs[output].id.c_str() : nullptr;
}
e51p_status e51p_decoder_select(const e51p_decoder* d, e51p_digital_state input, uint32_t* selected) {
    if (!d || !selected) return E51P_INVALID_ARGUMENT;
    *selected = E51P_INVALID_ID;
    const uint32_t mask = d->bits == 32 ? UINT32_MAX : (UINT32_C(1) << d->bits) - 1;
    if ((input.floating | input.contention) & mask) return E51P_INVALID_STATE;
    if (input.value & ~mask) return E51P_INVALID_ARGUMENT;
    const auto it = d->table.find(input.value);
    if (it == d->table.end()) return E51P_NOT_FOUND;
    *selected = it->second;
    return E51P_OK;
}
e51p_status e51p_decoder_level(const e51p_decoder* d, uint32_t output,
    e51p_digital_state input, e51p_digital_state* level) {
    if (!d || !level || output >= d->outputs.size()) return E51P_INVALID_ARGUMENT;
    uint32_t selected;
    const auto status = e51p_decoder_select(d, input, &selected);
    if (status != E51P_OK && status != E51P_NOT_FOUND) {
        *level = {0, status == E51P_INVALID_STATE ? 1u : 0u, 0};
        return status;
    }
    *level = {static_cast<uint32_t>((selected == output) != d->outputs[output].active_low), 0, 0};
    return E51P_OK;
}
}
