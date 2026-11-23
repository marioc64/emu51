// SPDX-License-Identifier: GPL-2.0-only
#pragma once
#include "emu51/displays.h"
// style: 0=bar, 7/14/16=segment geometry; colors: red/green/amber/blue/white.
void emu51DrawSegmentDisplay(const e51p_segment_snapshot&, int style, int color, bool instantaneous, bool colon, float zoom = 1);
