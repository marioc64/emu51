// SPDX-License-Identifier: LGPL-2.1-only
#pragma once
#include "emu51/registry.h"
#include <cstddef>
const e51p_factory_desc* const* e51p_builtin_factories(std::size_t& count);

const e51p_factory_desc* const* e51p_display_factories(std::size_t& count);
