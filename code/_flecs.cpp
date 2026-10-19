/*******************************************************************************
 *                                O P E N  T S
 *******************************************************************************
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 OpenTS contributors
 *
 * See LICENSE.md for applicable additional terms and warranty disclaimers.
 ******************************************************************************/

#include "flecs/world.h"

// The one EC world object. Its flecs world is created on first attach, not
// here, because globals such as the CellClass BlubCell sentinel can attach
// before this object is constructed.
FlecsWorldClass FlecsWorld;
