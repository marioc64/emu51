// SPDX-License-Identifier: GPL-2.0-only
#include "emu51_hardware.h"

namespace { Emu51HardwareFactory factory = nullptr; }

void emu51RegisterHardwareFactory(Emu51HardwareFactory value) { factory = value; }
std::unique_ptr<Emu51Hardware> emu51CreateHardware() { return factory ? factory() : nullptr; }
