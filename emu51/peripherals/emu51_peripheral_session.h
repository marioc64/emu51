// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "emu51_peripheral_config.h"
#include "emu51_peripheral_gpio.h"
#include "emu51/audio.h"
#include "emu51/components.h"
#include "emu51/displays.h"
#include <array>
#include <filesystem>
#include <functional>
#include <memory>
#include <vector>

struct e51p_audio_host;
struct Emu51PeripheralPin {
    std::string name;
    std::uint32_t kind;
    unsigned direction, width;
};

// One simulation-thread-owned graph. Detach CPU port I/O before destruction.
// Construction is transactional: create the replacement before touching CPU.
class Emu51PeripheralSession {
public:
    static std::unique_ptr<Emu51PeripheralSession> create(const Emu51PeripheralConfig&, double crystalMHz);
    ~Emu51PeripheralSession();
    Emu51PeripheralSession(const Emu51PeripheralSession&) = delete;
    Emu51PeripheralSession& operator=(const Emu51PeripheralSession&) = delete;
    static std::vector<Emu51PeripheralPin> describe(const Emu51PeripheralConfig&, bool includeBus = false);
    const Emu51PeripheralConfig& config() const { return config_; }
    std::shared_ptr<Emu51PeripheralGpio> gpio() const { return gpio_; }
    bool readData(std::uint16_t address, std::uint8_t& value, bool peek);
    bool writeData(std::uint16_t address, std::uint8_t value);
    e51p_status status() const;
    void resetStatus() { busStatus_ = E51P_OK; }
    std::uint64_t ticks() const;
    bool sampleDigital(const std::string&, e51p_digital_state&) const;
    bool sampleAnalog(const std::string&, double&) const;
    bool setSource(const std::string& id, double value);
    bool setPressed(const std::string& id, bool pressed);
    bool pressed(const std::string& id) const;
    bool setKeys(const std::string& id, uint32_t keys);
    uint32_t keys(const std::string& id) const;
    bool ledState(const std::string& id, bool& defined) const;
    bool lcdState(const std::string& id, e51p_lcd_snapshot&) const;
    bool displayState(const std::string& id, e51p_segment_snapshot&) const;
    bool setDisplayKeys(const std::string& id, uint64_t mask);
    uint64_t displayKeys(const std::string& id) const;
    // Snapshot on the simulation thread; writer may temporarily release the
    // host's execution lock for disk I/O, without touching graph state.
    bool flushStorage(std::string& error,
        const std::function<bool(const std::filesystem::path&, std::string_view)>& writer = {});
    void activateStorage() { storageActive_ = true; }
    void carryStorageFrom(const Emu51PeripheralSession&);

    // Enumeration/opening are explicit UI actions; never done by create/load.
    bool enumerateAudio(std::vector<std::string>& capture, std::vector<std::string>& playback,
        std::string& error, bool nullBackend = false);
    bool openAudio(int captureDevice, int playbackDevice, int captureChannel, int playbackChannel, std::string& error);
    void closeAudio();
    bool syncAudio(bool running, std::string& error);
    // Rebase only the bridge timeline after a host-clock discontinuity. Keeps
    // native devices open/running and preserves CPU, graph time and gain.
    bool resyncAudio(std::string& error);
    double audioLatencyMs() const { return audioLatencyMs_; }
    bool audioOpen() const { return audioOpen_; }
    bool audioRunning() const { return audioRunning_; }
    bool audioStats(e51p_audio_stats&) const;
    bool setInputGainDb(double db);
    bool setAudioBufferMs(int milliseconds, std::string& error);
    bool audioScope(e51p_audio_scope_frame*, uint32_t capacity, uint32_t& count, uint32_t& rate) const;
private:
    Emu51PeripheralSession() = default;
    void build(const Emu51PeripheralConfig&, double crystalMHz);
    bool claims(std::uint16_t address) const;
    e51p_endpoint endpoint(const std::string&) const;
    struct Source;
    Emu51PeripheralConfig config_;
    const e51p_api* api_ = nullptr;
    e51p_context* context_ = nullptr;
    std::shared_ptr<Emu51PeripheralGpio> gpio_;
    std::vector<std::pair<std::string, e51p_endpoint>> endpoints_;
    std::vector<std::pair<std::string, Source*>> sources_;
    struct Component {
        std::string id, storagePath;
        e51p_component* handle = nullptr;
        uint32_t kind = 0, keys = 0;
        bool pressed = false, storageSaved = false;
        std::array<uint8_t,256> savedBytes{};
    };
    std::vector<Component> components_;
    struct Display { std::string id; e51p_display* handle; uint64_t keys = 0; };
    std::vector<Display> displays_;
    bool built_ = false;
    bool storageActive_ = false;
    e51p_endpoint bus_ = E51P_INVALID_ID;
    e51p_status busStatus_ = E51P_OK;
    e51p_audio_stream* audio_ = nullptr;
    [[maybe_unused]] e51p_audio_host* audioHost_ = nullptr;
    bool audioOpen_ = false, audioRunning_ = false;
    double audioLatencyMs_ = 0;
    int audioCaptureDevice_ = -1, audioPlaybackDevice_ = -1;
    int audioCaptureChannel_ = 0, audioPlaybackChannel_ = -1;
};
