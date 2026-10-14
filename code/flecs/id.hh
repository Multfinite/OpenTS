/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#pragma once

#include <cstdint>

// The flecs entity that names one game object in this process. Zero names no
// object. The value is recycled with generation bits and differs between runs,
// so it is never saved, sent over the network, or folded into a checksum.
using EntityId = std::uint64_t;
