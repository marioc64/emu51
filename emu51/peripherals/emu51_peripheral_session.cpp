// SPDX-License-Identifier: GPL-2.0-only
#include "emu51_peripheral_session.h"
#include "emu51/registry.h"
#include "emu51_editor_files.h"
#include <fstream>
#ifdef EMU51_GUI_HOST_AUDIO
#include "emu51/audio_host.h"
#endif
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace {
void require(e51p_status s, const std::string& operation)
{
    if (s != E51P_OK) throw std::runtime_error(operation + " (SDK status " + std::to_string(s) + ")");
}
bool fatal(e51p_status s) { return s != E51P_OK && s != E51P_CONTENTION; }
e51p_registry* registry()
{
    static const auto catalog = [] {
        e51p_registry* raw = nullptr;
        require(e51p_registry_create(E51P_REGISTRY_ABI_VERSION, &raw), "Create device registry");
        std::unique_ptr<e51p_registry, decltype(&e51p_registry_destroy)> owned(raw, e51p_registry_destroy);
        require(e51p_registry_add_builtins(raw), "Register built-in devices");
        return owned;
    }();
    return catalog.get();
}
const char* factoryType(Emu51PeripheralKind kind)
{
    if (const int display=emu51PeripheralDisplayKind(kind); display>=0) return e51p_display_type_id(static_cast<unsigned>(display));
    if (const int component = emu51PeripheralComponentKind(kind); component >= 0) {
        static const char* types[]{"emu51.led","emu51.switch","emu51.button","emu51.hd44780","emu51.keypad4x4",
            "emu51.74hc595","emu51.74hc165","emu51.24c02","emu51.ay38910"};
        return types[component];
    }
    switch (kind) {
    case Emu51PeripheralKind::I8255: return "emu51.i8255.mode0";
    case Emu51PeripheralKind::Adc0804: return "emu51.adc0804";
    case Emu51PeripheralKind::Dac: return "emu51.parallel-dac";
    default: return nullptr;
    }
}
const e51p_factory_desc* factory(Emu51PeripheralKind kind)
{
    const auto* type = factoryType(kind);
    if (!type) return nullptr;
    const e51p_factory_desc* descriptor = nullptr;
    require(e51p_registry_find(registry(), type, &descriptor), "Find device factory");
    return descriptor;
}
}
namespace {
std::pair<uint32_t, uint32_t> audioBuffering(int directions, int milliseconds) {
    const uint32_t latency = milliseconds ? static_cast<uint32_t>(milliseconds) * 48u :
        ((directions & E51P_AUDIO_CAPTURE) ? 2048u : 6144u);
    uint32_t capacity = 16384;
    while (capacity < 2 * latency) capacity *= 2;
    return {capacity, latency};
}
}
struct Emu51PeripheralSession::Source {
    const e51p_api* api;
    e51p_context* context;
    e51p_endpoint pin = E51P_INVALID_ID;
    bool analog;
    double value;
    e51p_status publish() {
        return analog ? api->analog_write(context, pin, value) :
            api->drive(context, pin, static_cast<std::uint32_t>(value), 255, 0);
    }
    static void reset(void* user, e51p_context*) { (void)static_cast<Source*>(user)->publish(); }
    static void destroy(void* user) { delete static_cast<Source*>(user); }
};

std::vector<Emu51PeripheralPin> Emu51PeripheralSession::describe(const Emu51PeripheralConfig& c, bool includeBus)
{
    std::vector<Emu51PeripheralPin> pins;
    for (unsigned i = 0; i < 4; ++i)
        pins.push_back({"cpu.P" + std::to_string(i), E51P_DIGITAL, E51P_INPUT | E51P_OUTPUT, 8});
    if (includeBus) pins.push_back({"cpu.xdata", E51P_BUS_INITIATOR, E51P_OUTPUT, 8});
    for (const auto& d : c.devices) {
        const auto add = [&](const char* name, std::uint32_t kind, unsigned direction, unsigned width = 1) {
            pins.push_back({d.id + "." + name, kind, direction, width});
        };
        if (emu51PeripheralComponentKind(d.kind) >= 0 || emu51PeripheralDisplayKind(d.kind) >= 0) {
            const auto* descriptor = factory(d.kind);
            for (unsigned i = 0; i < descriptor->pin_count; ++i) {
                const auto& p = descriptor->pins[i];
                if (p.kind == E51P_DIGITAL || p.kind == E51P_ANALOG || includeBus) add(p.name,p.kind,p.direction,p.width);
            }
            continue;
        }
        switch (d.kind) {
        case Emu51PeripheralKind::I8255:
        case Emu51PeripheralKind::Adc0804:
        case Emu51PeripheralKind::Dac: {
            const auto* descriptor = factory(d.kind);
            for (unsigned i = 0; i < descriptor->pin_count; ++i) {
                const auto& p = descriptor->pins[i];
                if (p.kind == E51P_DIGITAL || p.kind == E51P_ANALOG || includeBus)
                    add(p.name, p.kind, p.direction, p.width);
            }
            break;
        }
        case Emu51PeripheralKind::Voltage: add("out", E51P_ANALOG, E51P_OUTPUT); break;
        case Emu51PeripheralKind::Digital: add("out", E51P_DIGITAL, E51P_OUTPUT, 8); break;
        case Emu51PeripheralKind::Audio:
            if (d.audioDirections & E51P_AUDIO_CAPTURE) add("capture", E51P_ANALOG, E51P_OUTPUT);
            if (d.audioDirections & E51P_AUDIO_PLAYBACK) add("playback", E51P_ANALOG, E51P_INPUT);
            break;
        default: break;
        }
    }
    return pins;
}
std::unique_ptr<Emu51PeripheralSession> Emu51PeripheralSession::create(const Emu51PeripheralConfig& c, double mhz)
{
    auto result = std::unique_ptr<Emu51PeripheralSession>(new Emu51PeripheralSession);
    result->build(c, mhz);
    return result;
}
Emu51PeripheralSession::~Emu51PeripheralSession()
{
    closeAudio();
    std::string storageError;
    if (built_ && storageActive_) (void)flushStorage(storageError);
#ifdef EMU51_GUI_HOST_AUDIO
    e51p_audio_host_destroy(audioHost_);
#endif
    gpio_.reset();
    if (context_) api_->destroy(context_);
    if (audio_) e51p_audio_release(audio_);
}
e51p_endpoint Emu51PeripheralSession::endpoint(const std::string& name) const
{
    const auto found = std::find_if(endpoints_.begin(), endpoints_.end(), [&](const auto& pin) { return pin.first == name; });
    if (found == endpoints_.end()) throw std::runtime_error("Unknown peripheral pin: " + name);
    return found->second;
}
void Emu51PeripheralSession::build(const Emu51PeripheralConfig& c, double mhz)
{
    emu51ValidatePeripheralConfig(c);
    if (!std::isfinite(mhz) || mhz < 0.000001 || mhz > 1000) throw std::runtime_error("Invalid peripheral clock");
    config_ = c;
    require(e51p_get_api(E51P_ABI_VERSION, &api_), "Get peripheral API");
    require(api_->create(static_cast<std::uint64_t>(std::llround(mhz * 1000000)), &context_), "Create graph");
    gpio_ = std::make_shared<Emu51PeripheralGpio>(*api_, *context_);
    for (unsigned i = 0; i < 4; ++i) endpoints_.emplace_back("cpu.P" + std::to_string(i), gpio_->port(i));
    e51p_device_desc host{ }; host.struct_size = sizeof(host); host.instance_id = "cpu-bus";
    host.user = this;
    host.reset = [](void* user, e51p_context*) { static_cast<Emu51PeripheralSession*>(user)->resetStatus(); };
    e51p_device hostId;
    require(api_->add_device(context_, &host, &hostId), "Add CPU bus");
    e51p_endpoint_desc busDesc{sizeof(busDesc), "xdata", E51P_BUS_INITIATOR, 0, 8, 16, 0, 0};
    require(api_->add_endpoint(context_, hostId, &busDesc, &bus_), "Add XDATA endpoint");
    for (const auto& d : c.devices) {
        const auto pin = [&](const char* name, e51p_endpoint ep) { endpoints_.emplace_back(d.id + "." + name, ep); };
        if (const int kind=emu51PeripheralDisplayKind(d.kind); kind>=0) {
            auto settings=e51p_display_config_init(static_cast<unsigned>(kind));
            settings.digits=static_cast<unsigned>(d.displayDigits); settings.bar_segments=static_cast<unsigned>(d.barSegments);
            settings.common_anode=d.commonAnode; settings.colon=d.displayColon; settings.persistence_ms=d.persistenceMs;
            settings.i2c_address=static_cast<unsigned>(d.displayAddress);
            e51p_endpoint handles[8]; e51p_display* handle=nullptr;
            require(e51p_add_display(api_,context_,d.id.c_str(),&settings,&handle,handles,8),"Create "+d.id);
            const auto* descriptor=factory(d.kind);
            for (unsigned i=0;i<descriptor->pin_count;++i) pin(descriptor->pins[i].name,handles[i]);
            displays_.push_back({d.id,handle,0});
            continue;
        }
        if (const int kind = emu51PeripheralComponentKind(d.kind); kind >= 0) {
            auto settings = e51p_component_config_init(static_cast<unsigned>(kind));
            settings.active_low = d.activeLow; settings.initially_pressed = d.initiallyPressed;
            settings.bounce_ticks = static_cast<uint64_t>(std::llround(mhz*1000*d.bounceMs));
            settings.columns = static_cast<unsigned>(d.columns); settings.rows = static_cast<unsigned>(d.rows);
            settings.i2c_address = static_cast<unsigned>(d.i2cAddress); settings.clock_hz = d.clockHz;
            e51p_endpoint handles[16]; e51p_component* handle = nullptr;
            require(e51p_add_component(api_,context_,d.id.c_str(),&settings,&handle,handles,16),"Create "+d.id);
            const auto* descriptor = factory(d.kind);
            for (unsigned i = 0; i < descriptor->pin_count; ++i) pin(descriptor->pins[i].name,handles[i]);
            if (const int size = emu51PeripheralBusSize(d.kind))
                require(api_->map_bus(context_,bus_,static_cast<unsigned>(d.address),static_cast<unsigned>(size),handles[0],0),"Map "+d.id);
            Component component; component.id = d.id; component.handle = handle;
            component.kind = static_cast<unsigned>(kind); component.pressed = d.initiallyPressed;
            component.savedBytes.fill(255);
            if (d.kind == Emu51PeripheralKind::Eeprom && !d.storagePath.empty()) {
                component.storagePath = d.storagePath;
                std::error_code error;
                const bool exists = std::filesystem::exists(d.storagePath,error);
                if (error) throw std::runtime_error("Cannot inspect EEPROM image: "+d.storagePath);
                if (exists) {
                    std::ifstream image(d.storagePath,std::ios::binary);
                    if (!image.read(reinterpret_cast<char*>(component.savedBytes.data()),256) || image.peek() != std::char_traits<char>::eof())
                        throw std::runtime_error("EEPROM image must contain exactly 256 bytes: "+d.storagePath);
                    require(e51p_component_eeprom_import(handle,component.savedBytes.data(),256),"Load EEPROM image");
                    component.storageSaved = true;
                }
            }
            components_.push_back(std::move(component));
            continue;
        }
        switch (d.kind) {
        case Emu51PeripheralKind::I8255:
        case Emu51PeripheralKind::Adc0804:
        case Emu51PeripheralKind::Dac: {
            const auto* descriptor = factory(d.kind);
            std::vector<e51p_parameter_value> values;
            if (d.kind == Emu51PeripheralKind::Adc0804) {
                values.push_back({sizeof(e51p_parameter_value), "conversion_ticks", E51P_PARAMETER_INTEGER, d.conversionTicks, 0, nullptr});
                values.push_back({sizeof(e51p_parameter_value), "full_scale_volts", E51P_PARAMETER_REAL, 0, d.referenceVolts, nullptr});
            } else if (d.kind == Emu51PeripheralKind::Dac) {
                values.push_back({sizeof(e51p_parameter_value), "reference_volts", E51P_PARAMETER_REAL, 0, d.referenceVolts, nullptr});
            }
            std::vector<e51p_endpoint> handles(descriptor->pin_count);
            require(e51p_registry_instantiate(registry(), descriptor->type_id, api_, context_, d.id.c_str(),
                values.data(), static_cast<unsigned>(values.size()), handles.data(), static_cast<unsigned>(handles.size())), "Create " + d.id);
            for (unsigned i = 0; i < descriptor->pin_count; ++i) pin(descriptor->pins[i].name, handles[i]);
            if (d.kind == Emu51PeripheralKind::I8255)
                require(api_->map_bus(context_, bus_, static_cast<unsigned>(d.address), 4, endpoint(d.id + ".bus"), 0), "Map 8255 XDATA");
            break;
        }
        case Emu51PeripheralKind::Voltage:
        case Emu51PeripheralKind::Digital: {
            auto source = std::make_unique<Source>();
            source->api = api_; source->context = context_; source->analog = d.kind == Emu51PeripheralKind::Voltage;
            source->value = source->analog ? d.voltage : d.digitalValue;
            e51p_device_desc desc{}; desc.struct_size = sizeof(desc); desc.instance_id = d.id.c_str();
            desc.user = source.get(); desc.reset = Source::reset; desc.destroy = Source::destroy;
            e51p_device device;
            require(api_->add_device(context_, &desc, &device), "Add source");
            auto* state = source.release(); // context owns it from here
            e51p_endpoint_desc p{sizeof(p), "out", state->analog ? E51P_ANALOG : E51P_DIGITAL,
                E51P_OUTPUT, state->analog ? 1u : 8u, 0, 0, 0};
            require(api_->add_endpoint(context_, device, &p, &state->pin), "Add source pin");
            pin("out", state->pin); sources_.emplace_back(d.id, state); break;
        }
        case Emu51PeripheralKind::Audio: {
            auto settings = e51p_audio_config_init(static_cast<unsigned>(d.audioDirections));
            // GUI execution advances in frame-sized batches. Cover frame and
            // native callback jitter instead of using the bridge's 10 ms default.
            const auto [capacity, latency] = audioBuffering(d.audioDirections, d.audioBufferMs);
            settings.capacity_frames = capacity;
            settings.latency_frames = latency;
            audioLatencyMs_ = 1000.0 * settings.latency_frames / settings.sample_rate;
            // Default playback mapping: (Covox byte - 128) / 128.
            e51p_audio_endpoints p{}; p.struct_size = sizeof(p);
            require(e51p_audio_create(api_, context_, d.id.c_str(), &settings, &audio_, &p), "Add audio bridge");
            require(e51p_audio_set_input_gain(audio_, std::pow(10.0, d.inputGainDb / 20.0)), "Set input gain");
            if (p.capture_voltage != E51P_INVALID_ID) pin("capture", p.capture_voltage);
            if (p.playback_voltage != E51P_INVALID_ID) pin("playback", p.playback_voltage);
            break;
        }
        default: break;
        }
    }
    for (const auto& w : c.digital)
        require(api_->connect_digital(context_, endpoint(w.a), static_cast<unsigned>(w.aBit),
            endpoint(w.b), static_cast<unsigned>(w.bBit), static_cast<unsigned>(w.width)), "Connect " + w.a + " to " + w.b);
    for (const auto& w : c.analog)
        require(api_->connect_analog(context_, endpoint(w.from), endpoint(w.to)), "Connect " + w.from + " to " + w.to);
    require(api_->start(context_), "Start graph");
    built_ = true;
    if (audio_) require(e51p_audio_set_paused(audio_, 1), "Pause audio");
}
bool Emu51PeripheralSession::claims(std::uint16_t a) const
{
    return std::any_of(config_.devices.begin(), config_.devices.end(), [&](const auto& d) {
        return emu51PeripheralBusSize(d.kind) && a >= d.address && a < d.address + emu51PeripheralBusSize(d.kind);
    });
}
bool Emu51PeripheralSession::readData(std::uint16_t a, std::uint8_t& value, bool peek)
{
    if (!claims(a)) return false;
    value = 0xff;
    std::uint32_t data = 255;
    const auto result = api_->bus_read(context_, bus_, a, peek ? E51P_PEEK : E51P_READ, &data);
    if (!peek && !fatal(busStatus_)) busStatus_ = result;
    if (result == E51P_OK || result == E51P_CONTENTION) value = static_cast<std::uint8_t>(data);
    return true;
}
bool Emu51PeripheralSession::writeData(std::uint16_t a, std::uint8_t value)
{
    if (!claims(a)) return false;
    const auto result = api_->bus_write(context_, bus_, a, value);
    if (!fatal(busStatus_)) busStatus_ = result;
    return true;
}
e51p_status Emu51PeripheralSession::status() const
{
    if (fatal(busStatus_)) return busStatus_;
    e51p_audio_stats stats{};
    if (audioStats(stats) && fatal(stats.status)) return stats.status;
    return gpio_->status();
}
std::uint64_t Emu51PeripheralSession::ticks() const
{
    std::uint64_t ticks = 0, hz = 0;
    (void)api_->clock(context_, &ticks, &hz); return ticks;
}
bool Emu51PeripheralSession::sampleDigital(const std::string& name, e51p_digital_state& sample) const
{
    const auto result = api_->sample(context_, endpoint(name), &sample);
    return result == E51P_OK || result == E51P_CONTENTION;
}
bool Emu51PeripheralSession::sampleAnalog(const std::string& name, double& value) const
{
    return api_->analog_read(context_, endpoint(name), &value) == E51P_OK;
}
bool Emu51PeripheralSession::setSource(const std::string& id, double value)
{
    if (!std::isfinite(value) || std::abs(value) > 1000) return false;
    for (const auto& [name, source] : sources_) {
        if (name != id) continue;
        if (!source->analog && (value < 0 || value > 255 || std::floor(value) != value)) return false;
        source->value = value;
        const auto result = source->publish();
        if (fatal(result)) return false;
        for (auto& d : config_.devices) if (d.id == id) {
            if (source->analog) d.voltage = value; else d.digitalValue = static_cast<int>(value);
        }
        return true;
    }
    return false;
}
bool Emu51PeripheralSession::setPressed(const std::string& id, bool value)
{
    for (auto& c : components_) if (c.id == id && e51p_component_set_pressed(c.handle,value) == E51P_OK) {
        c.pressed = value;
        if (c.kind == E51P_COMPONENT_SWITCH)
            for (auto& d : config_.devices) if (d.id == id) d.initiallyPressed = value;
        return true;
    }
    return false;
}
bool Emu51PeripheralSession::pressed(const std::string& id) const
{
    for (const auto& c : components_) if (c.id == id) return c.pressed;
    return false;
}
bool Emu51PeripheralSession::setKeys(const std::string& id, uint32_t mask)
{
    for (auto& c : components_) if (c.id == id && e51p_component_set_keys(c.handle,mask) == E51P_OK) { c.keys = mask; return true; }
    return false;
}
uint32_t Emu51PeripheralSession::keys(const std::string& id) const
{
    for (const auto& c : components_) if (c.id == id) return c.keys;
    return 0;
}
bool Emu51PeripheralSession::ledState(const std::string& id, bool& defined) const
{
    int on = 0, known = 0;
    for (const auto& c : components_) if (c.id == id && e51p_component_get_led(c.handle,&on,&known) == E51P_OK) break;
    defined = known != 0; return on != 0;
}
bool Emu51PeripheralSession::lcdState(const std::string& id, e51p_lcd_snapshot& state) const
{
    state.struct_size = sizeof(state);
    for (const auto& c : components_) if (c.id == id) return e51p_component_get_lcd(c.handle,&state) == E51P_OK;
    return false;
}
bool Emu51PeripheralSession::displayState(const std::string& id,e51p_segment_snapshot& state) const
{
    state.struct_size=sizeof(state);
    for (const auto& d : displays_) if (d.id==id) return e51p_display_snapshot(d.handle,&state)==E51P_OK;
    return false;
}
bool Emu51PeripheralSession::setDisplayKeys(const std::string& id,uint64_t mask)
{
    for (auto& d : displays_) if (d.id==id && e51p_display_set_keys(d.handle,mask)==E51P_OK) { d.keys=mask; return true; }
    return false;
}
uint64_t Emu51PeripheralSession::displayKeys(const std::string& id) const
{
    for (const auto& d : displays_) if (d.id==id) return d.keys;
    return 0;
}
void Emu51PeripheralSession::carryStorageFrom(const Emu51PeripheralSession& previous)
{
    for (auto& c : components_) if (!c.storagePath.empty())
        for (const auto& old : previous.components_) if (old.storagePath == c.storagePath) {
            std::array<uint8_t,256> bytes{};
            if (e51p_component_eeprom_export(old.handle,bytes.data(),256) == E51P_OK)
                (void)e51p_component_eeprom_import(c.handle,bytes.data(),256);
        }
}
bool Emu51PeripheralSession::flushStorage(std::string& error,
    const std::function<bool(const std::filesystem::path&,std::string_view)>& writer)
{
    error.clear();
    for (auto& c : components_) if (!c.storagePath.empty()) {
        std::array<uint8_t,256> bytes{};
        if (e51p_component_eeprom_export(c.handle,bytes.data(),256) != E51P_OK) return false;
        if (c.storageSaved && c.savedBytes == bytes) continue;
        const std::string_view data(reinterpret_cast<const char*>(bytes.data()),bytes.size());
        const bool saved = writer ? writer(c.storagePath,data) : emu51WriteTextFileAtomically(c.storagePath,data);
        if (!saved) { error = "Cannot save EEPROM image: "+c.storagePath; return false; }
        c.savedBytes = bytes; c.storageSaved = true;
    }
    return true;
}
bool Emu51PeripheralSession::setInputGainDb(double db)
{
    if (!audio_ || !std::isfinite(db) || db < -20 || db > 60 ||
        e51p_audio_set_input_gain(audio_, std::pow(10.0, db / 20.0)) != E51P_OK) return false;
    for (auto& d : config_.devices) if (d.kind == Emu51PeripheralKind::Audio) d.inputGainDb = db;
    return true;
}
bool Emu51PeripheralSession::audioScope(e51p_audio_scope_frame* out, uint32_t capacity, uint32_t& count, uint32_t& rate) const
{
    return audio_ && e51p_audio_read_scope(audio_, out, capacity, &count, &rate) == E51P_OK;
}
bool Emu51PeripheralSession::audioStats(e51p_audio_stats& stats) const
{
    stats.struct_size = sizeof(stats);
    return audio_ && e51p_audio_get_stats(audio_, &stats) == E51P_OK;
}

bool Emu51PeripheralSession::enumerateAudio(std::vector<std::string>& capture, std::vector<std::string>& playback,
    std::string& error, bool nullBackend)
{
    error.clear(); capture.clear(); playback.clear();
#ifdef EMU51_GUI_HOST_AUDIO
    if (!audio_) { error = "Apply a graph containing host audio first"; return false; }
    if (!audioHost_) {
        const auto s = e51p_audio_host_create(nullBackend ? E51P_AUDIO_NULL : E51P_AUDIO_NATIVE, &audioHost_);
        if (s != E51P_OK) { error = "Unable to enumerate audio devices: " + std::to_string(s); return false; }
    }
    for (const auto direction : {E51P_AUDIO_CAPTURE, E51P_AUDIO_PLAYBACK}) {
        std::uint32_t count = 0;
        require(e51p_audio_host_device_count(audioHost_, static_cast<unsigned>(direction), &count), "Enumerate audio devices");
        auto& list = direction == E51P_AUDIO_CAPTURE ? capture : playback;
        for (std::uint32_t i = 0; i < count; ++i) {
            e51p_audio_device_info info{}; info.struct_size = sizeof(info);
            require(e51p_audio_host_device_info(audioHost_, static_cast<unsigned>(direction), i, &info), "Read audio device");
            list.emplace_back(std::string(info.name) + (info.is_default ? " (default)" : ""));
        }
    }
    return true;
#else
    (void)nullBackend;
    error = "Build with EMU51_PERIPHERAL_HOST_AUDIO=ON for native audio"; return false;
#endif
}
bool Emu51PeripheralSession::openAudio(int captureDevice, int playbackDevice, int captureChannel, int playbackChannel, std::string& error)
{
    error.clear();
#ifdef EMU51_GUI_HOST_AUDIO
    if (!audio_ || !audioHost_) { error = "List audio devices first"; return false; }
    if (captureDevice < -1 || playbackDevice < -1 || captureChannel < 0 || playbackChannel < -1) {
        error = "Invalid audio device or channel"; return false;
    }
    closeAudio();
    auto cfg = e51p_audio_host_config_init();
    cfg.capture_device = static_cast<unsigned>(captureDevice); cfg.playback_device = static_cast<unsigned>(playbackDevice);
    cfg.capture_channel = static_cast<unsigned>(captureChannel); cfg.playback_channel = static_cast<unsigned>(playbackChannel);
    const auto s = e51p_audio_host_open(audioHost_, audio_, &cfg);
    if (s != E51P_OK) {
        char message[512]{}; (void)e51p_audio_host_error(audioHost_, message, sizeof(message));
        error = *message ? message : "Unable to open audio device"; return false;
    }
    audioCaptureDevice_ = captureDevice; audioPlaybackDevice_ = playbackDevice;
    audioCaptureChannel_ = captureChannel; audioPlaybackChannel_ = playbackChannel;
    audioOpen_ = true; return true;
#else
    (void)captureDevice; (void)playbackDevice; (void)captureChannel; (void)playbackChannel;
    error = "Native audio is disabled in this build"; return false;
#endif
}
void Emu51PeripheralSession::closeAudio()
{
#ifdef EMU51_GUI_HOST_AUDIO
    if (audioHost_) e51p_audio_host_close(audioHost_);
#endif
    audioOpen_ = audioRunning_ = false;
}
bool Emu51PeripheralSession::setAudioBufferMs(int milliseconds, std::string& error)
{
    error.clear();
    if (!audio_ || !emu51ValidAudioBufferMs(milliseconds)) {
        error = "Audio buffer must be 10..2000 ms, or 0 for automatic.";
        return false;
    }
    auto device = std::find_if(config_.devices.begin(), config_.devices.end(), [](const auto& d) {
        return d.kind == Emu51PeripheralKind::Audio;
    });
    if (device == config_.devices.end()) return false;
    if (device->audioBufferMs == milliseconds) return true;
    const auto [capacity, latency] = audioBuffering(device->audioDirections, milliseconds);
    const bool wasOpen = audioOpen_, wasRunning = audioRunning_;
    closeAudio(); // joins callbacks and detaches the host before queue replacement
    auto status = e51p_audio_set_paused(audio_, 1);
    if (!fatal(status)) status = e51p_audio_set_buffering(audio_, capacity, latency);
    const bool resized = !fatal(status);
    if (resized) {
        device->audioBufferMs = milliseconds;
        audioLatencyMs_ = 1000.0 * latency / 48000;
    }
    std::string reopenError;
    if (wasOpen && (!openAudio(audioCaptureDevice_, audioPlaybackDevice_, audioCaptureChannel_, audioPlaybackChannel_, reopenError) ||
        (wasRunning && !syncAudio(true, reopenError)))) {
        error = "Audio buffer " + std::string(resized ? "changed; " : "unchanged; ") + reopenError;
        return false;
    }
    if (!resized) error = "Unable to resize audio buffers (SDK status " + std::to_string(status) + ")";
    return resized;
}
bool Emu51PeripheralSession::resyncAudio(std::string& error)
{
    error.clear();
    if (!audio_ || !audioRunning_) return true;
    auto result = e51p_audio_set_paused(audio_, 1);
    if (!fatal(result)) result = e51p_audio_set_paused(audio_, 0);
    if (fatal(result)) {
        error = "Audio timing resynchronization failed (SDK status " + std::to_string(result) + ")";
        return false;
    }
    return true;
}
bool Emu51PeripheralSession::syncAudio(bool running, std::string& error)
{
    error.clear();
#ifdef EMU51_GUI_HOST_AUDIO
    if (!audioOpen_ || running == audioRunning_) return true;
    const auto result = running ? e51p_audio_host_start(audioHost_) : e51p_audio_host_stop(audioHost_);
    if (result != E51P_OK) {
        char message[512]{}; (void)e51p_audio_host_error(audioHost_, message, sizeof(message));
        error = *message ? message : "Audio start/stop failed"; closeAudio(); return false;
    }
    audioRunning_ = running;
#else
    (void)running;
#endif
    return true;
}
