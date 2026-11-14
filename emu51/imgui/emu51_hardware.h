// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

class Emu51HardwareHost {
public:
    virtual ~Emu51HardwareHost() = default;
    virtual bool loadHardwareProgram(const std::filesystem::path& path, std::uint16_t address,
        bool unified, double crystalMHz) = 0;
    virtual void resetHardwareCpu() = 0;
    virtual const std::string& hardwareStatus() const = 0;
};

// Optional local hardware. Returning true claims a data-space bus access;
// program fetches and image loading always retain the application's normal map.
class Emu51Hardware {
public:
    virtual ~Emu51Hardware() = default;
    virtual const char* id() const = 0;
    virtual const char* name() const = 0;
    virtual bool enabled() const = 0;
    virtual void setEnabled(bool enabled) = 0;
    virtual void reset() = 0;
    virtual bool readData(std::uint16_t address, std::uint8_t& value) const = 0;
    virtual bool writeData(std::uint16_t address, std::uint8_t value) = 0;
    virtual void advance(unsigned cycles, double crystalMHz) = 0;
    virtual void show() = 0;
    virtual void draw(Emu51HardwareHost& host) = 0;
    virtual bool wantsRedraw() const { return false; }
};

using Emu51HardwareFactory = std::unique_ptr<Emu51Hardware> (*)();
// Local extension object files register a factory during startup. The standard
// build registers none and never looks for modules or firmware on disk.
void emu51RegisterHardwareFactory(Emu51HardwareFactory factory);
std::unique_ptr<Emu51Hardware> emu51CreateHardware();
