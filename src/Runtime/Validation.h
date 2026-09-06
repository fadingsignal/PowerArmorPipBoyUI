#pragma once

#include "Profiles.h"

#include <algorithm>
#include <cstring>
#include <limits>

namespace PowerArmorPipBoyUI::Runtime::Validation
{
[[nodiscard]] constexpr bool Contains(std::uint64_t a_start, std::uint64_t a_size,
                                      std::uint64_t a_address, std::uint64_t a_length)
{
	return a_address >= a_start && a_length <= a_size && a_address - a_start <= a_size - a_length;
}

[[nodiscard]] inline bool DirectCallMatches(std::span<const std::uint8_t> a_bytes,
                                            std::uint64_t a_address, std::uint64_t a_target)
{
	if (a_bytes.size() < 5 || a_bytes[0] != 0xE8 ||
	    a_address > std::numeric_limits<std::uint64_t>::max() - 5) {
		return false;
	}
	std::int32_t displacement;
	std::memcpy(&displacement, a_bytes.data() + 1, sizeof(displacement));
	const auto next = a_address + 5;
	if (displacement < 0) {
		const auto distance = static_cast<std::uint64_t>(-static_cast<std::int64_t>(displacement));
		return next >= distance && next - distance == a_target;
	}
	const auto distance = static_cast<std::uint64_t>(displacement);
	return next <= std::numeric_limits<std::uint64_t>::max() - distance &&
	       next + distance == a_target;
}

// Fixed context validation, not a signature scan or proof of every layout.
// Unlike the adjacent inverse helper, setup saves AL in ESI and later tests
// SIL to select the PA renderer branch. Displacements of relocated calls and
// the RIP-relative player load are intentionally excluded from the byte check.
[[nodiscard]] inline bool SetupMatches(std::span<const std::uint8_t> a_function,
                                       std::uint32_t a_callOffset,
                                       const Profiles::SetupGuard& a_guard)
{
	constexpr std::array<std::uint8_t, 4> savedResult{0x0F, 0xB6, 0xF0, 0xE8};
	if (a_callOffset != a_guard.prefix.size() + 4 ||
	    !Contains(0, a_function.size(), a_callOffset, 5 + savedResult.size()) ||
	    !Contains(0, a_function.size(), a_guard.branchOffset, a_guard.branch.size())) {
		return false;
	}
	return std::equal(a_guard.prefix.begin(), a_guard.prefix.end(), a_function.begin()) &&
	       std::equal(savedResult.begin(), savedResult.end(),
	                  a_function.begin() + a_callOffset + 5) &&
	       std::equal(a_guard.branch.begin(), a_guard.branch.end(),
	                  a_function.begin() + a_guard.branchOffset);
}
} // namespace PowerArmorPipBoyUI::Runtime::Validation
