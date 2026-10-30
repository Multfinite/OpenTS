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
#include <string_view>

namespace EC {

// Compile-time 64-bit FNV-1a hash used to derive stable, compiler-independent
// component type identifiers for .SAV trailers and per-component CRC tags.
constexpr std::uint64_t Fnv1a_64(std::string_view text) noexcept
{
	std::uint64_t hash = 0xcbf29ce484222325ULL;
	for (unsigned char const ch : text) {
		hash ^= static_cast<std::uint64_t>(ch);
		hash *= 0x100000001b3ULL;
	}
	return hash;
}

constexpr std::uint32_t Fnv1a_32_Fold(std::uint64_t hash64) noexcept
{
	return static_cast<std::uint32_t>((hash64 >> 32) ^ (hash64 & 0xFFFFFFFFULL));
}

} // namespace EC
