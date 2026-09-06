#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

// Shared by the DLL and the offline audit tool. These are exact executable
// profiles, not generic OG/AE defaults. New versions require their own evidence.
namespace PowerArmorPipBoyUI::Runtime::Profiles
{
using Version = std::array<std::uint16_t, 4>;

enum class CallKind
{
	kPresentation,
	kOpenAudio,
	kCloseAudio,
	kLight,
	kClosed,
	kOpen,
	kClose,
	kHolotape
};

struct CallSite
{
	CallKind kind;
	std::uint64_t containingID;
	std::uint32_t offset;
	std::uint64_t targetID;
	std::string_view description;
	bool rendererSetup{false};
};

enum class VtableKind
{
	kPipboyFrame,
	kInputShouldHandle,
	kInputButton,
	kFirstPersonUpdate,
	kHUDFrame
};

struct VtableSite
{
	VtableKind kind;
	std::uint64_t tableID;
	std::size_t slot;
	std::uint64_t targetID;
	std::string_view description;
};

enum class ActiveSetterABI
{
	kValueEventSource,
	kPipboyManager
};

struct CallableIDs
{
	std::uint64_t actorInPowerArmor;
	std::uint64_t activeSetter;
	std::uint64_t rainModifier;
	std::uint64_t referenceIsInterior;
	std::uint64_t submergeLevel;
};

struct SetupGuard
{
	// Entry through MOV RCX,[RIP+disp32], excluding its displacement.
	std::span<const std::uint8_t> prefix;
	// The saved predicate controls TEST SIL,SIL; JZ <non-PA branch>.
	std::uint32_t branchOffset;
	std::array<std::uint8_t, 5> branch;
};

struct Profile
{
	Version version;
	std::string_view name;
	std::span<const CallSite> calls;
	std::span<const VtableSite> vtables;
	CallableIDs functions;
	ActiveSetterABI activeSetterABI;
	SetupGuard setup;
};

inline constexpr std::array kOG163Calls{
	CallSite{CallKind::kPresentation, 394568, 0x30, 1176757, "AddMenuToPipboy (viewport rect)"},
	CallSite{CallKind::kPresentation, 1444875, 0x46, 1176757, "LowerPipboy (skip lower anim)"},
	CallSite{CallKind::kPresentation, 726763, 0x50, 1176757, "RaisePipboy (cursor constraint)"},
	CallSite{CallKind::kPresentation, 726763, 0xC6, 1176757, "RaisePipboy (skip raise anim)"},
	CallSite{CallKind::kPresentation, 273927, 0x87, 1176757, "PlayPipboyCloseAnim"},
	CallSite{CallKind::kPresentation, 302903, 0x19, 1176757, "ProcessLoweringReason"},
	CallSite{CallKind::kPresentation, 302903, 0x29, 1176757, "ProcessLoweringReason"},
	CallSite{CallKind::kPresentation, 900802, 0x1F, 1176757, "UpdateCursorConstraint"},
	CallSite{CallKind::kPresentation, 643948, 0x3BD, 1176757,
             "PipboyMenu screen-space hit testing"},
	CallSite{CallKind::kPresentation, 157921, 0x19, 1176757,
             "render-surface setup (screen-attached quad)", true},
	CallSite{CallKind::kPresentation, 1477369, 0x154, 1176757,
             "Pip-Boy opened (item preview camera)"},
	CallSite{CallKind::kPresentation, 1477369, 0x24C, 1176757,
             "Pip-Boy opened (screen shader + TAA mode)"},
	CallSite{CallKind::kPresentation, 1477369, 0x2A6, 1176757,
             "Pip-Boy opened (cursor constraint)"},
	CallSite{CallKind::kPresentation, 188351, 0x3AC, 1176757,
             "Terminal holotape render-target dimensions"},
	CallSite{CallKind::kPresentation, 926335, 0x860, 1176757,
             "Terminal holotape screen-space hit testing"},
	CallSite{CallKind::kOpenAudio, 1477369, 0xE6, 1176757,
             "Pip-Boy opened (power-armor open sound)"},
	CallSite{CallKind::kCloseAudio, 731410, 0xEF, 1176757,
             "ClosedownPipboy (power-armor close sound)"},
	CallSite{CallKind::kLight, 1477369, 0x313, 1176757, "Pip-Boy opened (keep Pip-Boy light on)"},
	CallSite{CallKind::kClosed, 1231000, 0x1D3, 592088, "OnPipboyCloseAnim -> OnPipboyClosed"},
	CallSite{CallKind::kClosed, 261739, 0x81, 592088,
             "PipboyManager event handler -> OnPipboyClosed"},
	CallSite{CallKind::kOpen, 181358, 0x23F, 663900, "Tab handler -> PlayPipboyOpenAnim"},
	CallSite{CallKind::kOpen, 943894, 0x134, 663900, "companion use-item -> PlayPipboyOpenAnim"},
	CallSite{CallKind::kClose, 273179, 0x3A, 273927, "PlayPipboyCloseAnim caller 1"},
	CallSite{CallKind::kClose, 643948, 0x232, 273927, "PlayPipboyCloseAnim caller 2"},
	CallSite{CallKind::kClose, 643948, 0x245, 273927, "PlayPipboyCloseAnim caller 3"},
	CallSite{CallKind::kClose, 926335, 0x3E8, 273927, "PlayPipboyCloseAnim caller 4"},
	CallSite{CallKind::kClose, 1299608, 0x48, 273927, "PlayPipboyCloseAnim caller 5"},
	CallSite{CallKind::kClose, 301794, 0x105, 273927, "PlayPipboyCloseAnim caller 6"},
	CallSite{CallKind::kHolotape, 634650, 0x9E, 477096, "animated holotape load caller 1"},
	CallSite{CallKind::kHolotape, 1411297, 0x1BD, 477096, "animated holotape load caller 2"},
};

inline constexpr std::array kAE221Calls{
	CallSite{CallKind::kPresentation, 2225453, 0x30, 2219437, "AddMenuToPipboy (viewport rect)"},
	CallSite{CallKind::kPresentation, 2225454, 0x46, 2219437, "LowerPipboy (skip lower anim)"},
	CallSite{CallKind::kPresentation, 2225455, 0x50, 2219437, "RaisePipboy (cursor constraint)"},
	CallSite{CallKind::kPresentation, 2225455, 0xC6, 2219437, "RaisePipboy (skip raise anim)"},
	CallSite{CallKind::kPresentation, 2225456, 0x8B, 2219437, "PlayPipboyCloseAnim"},
	CallSite{CallKind::kPresentation, 2225486, 0x19, 2219437, "ProcessLoweringReason"},
	CallSite{CallKind::kPresentation, 2225486, 0x29, 2219437, "ProcessLoweringReason"},
	CallSite{CallKind::kPresentation, 2225488, 0x1F, 2219437, "UpdateCursorConstraint"},
	CallSite{CallKind::kPresentation, 2224181, 0x43E, 2219437,
             "PipboyMenu screen-space hit testing"},
	CallSite{CallKind::kPresentation, 2225484, 0x12, 2219437,
             "render-surface setup (screen-attached quad)", true},
	CallSite{CallKind::kPresentation, 2225479, 0x154, 2219437,
             "Pip-Boy opened (item preview camera)"},
	CallSite{CallKind::kPresentation, 2225479, 0x24C, 2219437,
             "Pip-Boy opened (screen shader + TAA mode)"},
	CallSite{CallKind::kPresentation, 2225479, 0x2A6, 2219437,
             "Pip-Boy opened (cursor constraint)"},
	CallSite{CallKind::kPresentation, 2224616, 0x371, 2219437,
             "Terminal holotape render-target dimensions"},
	CallSite{CallKind::kPresentation, 2224614, 0x7E3, 2219437,
             "Terminal holotape screen-space hit testing"},
	CallSite{CallKind::kOpenAudio, 2225479, 0xEC, 2219437,
             "Pip-Boy opened (power-armor open sound)"},
	CallSite{CallKind::kCloseAudio, 2225480, 0x12B, 2219437,
             "ClosedownPipboy (power-armor close sound)"},
	CallSite{CallKind::kLight, 2225479, 0x313, 2219437, "Pip-Boy opened (keep Pip-Boy light on)"},
	CallSite{CallKind::kClosed, 2225457, 0x1ED, 2225458, "OnPipboyCloseAnim -> OnPipboyClosed"},
	CallSite{CallKind::kClosed, 2225493, 0x78, 2225458,
             "PipboyManager event handler -> OnPipboyClosed"},
	CallSite{CallKind::kOpen, 2249426, 0x32F, 2225444, "Tab handler -> PlayPipboyOpenAnim"},
	CallSite{CallKind::kOpen, 2219795, 0x134, 2225444, "companion use-item -> PlayPipboyOpenAnim"},
	CallSite{CallKind::kClose, 2231392, 0x232, 2225456, "PlayPipboyCloseAnim caller 1"},
	CallSite{CallKind::kClose, 2223217, 0x3F, 2225456, "PlayPipboyCloseAnim caller 2"},
	CallSite{CallKind::kClose, 2231408, 0x105, 2225456, "PlayPipboyCloseAnim caller 3"},
	CallSite{CallKind::kClose, 2224181, 0x21D, 2225456, "PlayPipboyCloseAnim caller 4"},
	CallSite{CallKind::kClose, 2224181, 0x230, 2225456, "PlayPipboyCloseAnim caller 5"},
	CallSite{CallKind::kClose, 2224614, 0x346, 2225456, "PlayPipboyCloseAnim caller 6"},
	CallSite{CallKind::kHolotape, 2224162, 0x147, 2225446, "animated holotape load caller 1"},
	CallSite{CallKind::kHolotape, 2224169, 0x9E, 2225446, "animated holotape load caller 2"},
	CallSite{CallKind::kHolotape, 2234886, 0x1B3, 2225446, "animated holotape load caller 3"},
};

inline constexpr std::array kOG163Vtables{
	VtableSite{VtableKind::kPipboyFrame, 5497, 4, 561368, "PipboyMenu AdvanceMovie"},
	VtableSite{VtableKind::kInputShouldHandle, 85678, 1, 607291, "PipboyMenu ShouldHandleEvent"},
	VtableSite{VtableKind::kInputButton, 85678, 8, 75248, "PipboyMenu OnButtonEvent"},
	VtableSite{VtableKind::kFirstPersonUpdate, 246953, 0x0B, 1392051, "FirstPersonState Update"},
	VtableSite{VtableKind::kHUDFrame, 585176, 4, 494688, "HUDMenu AdvanceMovie"},
};

inline constexpr std::array kAE221Vtables{
	VtableSite{VtableKind::kPipboyFrame, 5497, 4, 2224183, "PipboyMenu AdvanceMovie"},
	VtableSite{VtableKind::kInputShouldHandle, 85678, 1, 2224188, "PipboyMenu ShouldHandleEvent"},
	VtableSite{VtableKind::kInputButton, 85678, 8, 2224189, "PipboyMenu OnButtonEvent"},
	VtableSite{VtableKind::kFirstPersonUpdate, 246953, 0x0B, 2248260, "FirstPersonState Update"},
	VtableSite{VtableKind::kHUDFrame, 585176, 4, 2248853, "HUDMenu AdvanceMovie"},
};

inline constexpr std::array<std::uint8_t, 21> kOG163SetupPrefix{
	0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57,
	0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xD9, 0x48, 0x8B, 0x0D};
inline constexpr std::array<std::uint8_t, 14> kAE221SetupPrefix{
	0x40, 0x53, 0x56, 0x57, 0x48, 0x83, 0xEC, 0x30, 0x48, 0x8B, 0xD9, 0x48, 0x8B, 0x0D};

inline constexpr Profile kOG163{{1, 10, 163, 0},
                                "OG 1.10.163",
                                kOG163Calls,
                                kOG163Vtables,
                                {1176757, 318434, 547006, 1108031, 44316},
                                ActiveSetterABI::kValueEventSource,
                                {kOG163SetupPrefix, 0x77, {0x40, 0x84, 0xF6, 0x74, 0x79}}};
inline constexpr Profile kAE221{{1, 11, 221, 0},
                                "AE 1.11.221",
                                kAE221Calls,
                                kAE221Vtables,
                                {2219437, 2225492, 2199993, 2201166, 2229864},
                                ActiveSetterABI::kPipboyManager,
                                {kAE221SetupPrefix, 0x70, {0x40, 0x84, 0xF6, 0x74, 0x7C}}};
// 1.11.240 keeps the audited call topology, slots, helper ABIs, and setup
// context. Its Address Library supplies the relocated addresses. This is an
// explicit version entry, not permission to load on arbitrary later builds.
// Static evidence: docs/ae240_compatibility.md; in-game validation is pending.
inline constexpr Profile kAE240{{1, 11, 240, 0}, "AE 1.11.240",    kAE221.calls,
                                kAE221.vtables,  kAE221.functions, kAE221.activeSetterABI,
                                kAE221.setup};
inline constexpr std::array kProfiles{&kOG163, &kAE221, &kAE240};

// Structural omissions must fail the build, before they can leave an
// installer callback without its required address or original function.
[[nodiscard]] constexpr bool Complete(const Profile& a_profile)
{
	std::array<unsigned, 8> callCounts{};
	std::array<std::uint64_t, 8> callTargets{};
	std::array<unsigned, 5> vtableCounts{};
	unsigned setupCount = 0;
	for (const auto& site : a_profile.calls) {
		const auto kind = static_cast<std::size_t>(site.kind);
		if (kind >= callCounts.size() || !site.containingID || !site.targetID ||
		    site.description.empty()) {
			return false;
		}
		if (callCounts[kind] && callTargets[kind] != site.targetID) {
			return false; // Shared wrappers must have the same original callee.
		}
		callTargets[kind] = site.targetID;
		++callCounts[kind];
		if (site.rendererSetup) {
			if (site.kind != CallKind::kPresentation ||
			    site.offset != a_profile.setup.prefix.size() + 4) {
				return false;
			}
			++setupCount;
		}
	}
	for (const auto count : callCounts) {
		if (!count) {
			return false;
		}
	}
	for (std::size_t kind = 0; kind < 4; ++kind) {
		if (callTargets[kind] != a_profile.functions.actorInPowerArmor) {
			return false;
		}
	}
	for (const auto& site : a_profile.vtables) {
		const auto kind = static_cast<std::size_t>(site.kind);
		if (kind >= vtableCounts.size() || !site.tableID || !site.targetID ||
		    ++vtableCounts[kind] != 1) {
			return false;
		}
	}
	for (const auto count : vtableCounts) {
		if (count != 1) {
			return false;
		}
	}
	return setupCount == 1 && callCounts[static_cast<std::size_t>(CallKind::kOpen)] == 2 &&
	       a_profile.functions.activeSetter && a_profile.functions.rainModifier &&
	       a_profile.functions.referenceIsInterior && a_profile.functions.submergeLevel;
}
static_assert(
	[] {
		for (std::size_t i = 0; i < kProfiles.size(); ++i) {
			if (!Complete(*kProfiles[i])) {
				return false;
			}
			for (std::size_t j = 0; j < i; ++j) {
				if (kProfiles[i]->version == kProfiles[j]->version) {
					return false;
				}
			}
		}
		return true;
	}(),
	"Runtime profiles must be complete and have unique executable versions");

[[nodiscard]] constexpr const Profile* Find(const Version& a_version)
{
	for (const auto* profile : kProfiles) {
		if (profile->version == a_version) {
			return profile;
		}
	}
	return nullptr;
}
} // namespace PowerArmorPipBoyUI::Runtime::Profiles
