#include "Runtime.h"

namespace PowerArmorPipBoyUI::Runtime
{
	namespace
	{
		enum class Family
		{
			kOG,
			kAE
		};

		struct CallSite
		{
			std::uint64_t ogID;
			std::ptrdiff_t ogOffset;
			std::uint64_t aeID;
			std::ptrdiff_t aeOffset;
			std::string_view desc;
		};

		constexpr std::array kPresentationSites{
			CallSite{ 394568, 0x30, 2225453, 0x30, "AddMenuToPipboy (viewport rect)"sv },
			CallSite{ 1444875, 0x46, 2225454, 0x46, "LowerPipboy (skip lower anim)"sv },
			CallSite{ 726763, 0x50, 2225455, 0x50, "RaisePipboy (cursor constraint)"sv },
			CallSite{ 726763, 0xC6, 2225455, 0xC6, "RaisePipboy (skip raise anim)"sv },
			CallSite{ 273927, 0x87, 2225456, 0x8B, "PlayPipboyCloseAnim"sv },
			CallSite{ 302903, 0x19, 2225486, 0x19, "ProcessLoweringReason"sv },
			CallSite{ 302903, 0x29, 2225486, 0x29, "ProcessLoweringReason"sv },
			CallSite{ 900802, 0x1F, 2225488, 0x1F, "UpdateCursorConstraint"sv },
			CallSite{ 643948, 0x3BD, 2224181, 0x43E, "PipboyMenu screen-space hit testing"sv },
			CallSite{ 157921, 0x19, 2225483, 0x0B, "render-surface setup (screen-attached quad)"sv },
			CallSite{ 1477369, 0x154, 2225479, 0x154, "Pip-Boy opened (item preview camera)"sv },
			CallSite{ 1477369, 0x24C, 2225479, 0x24C, "Pip-Boy opened (screen shader + TAA mode)"sv },
			CallSite{ 1477369, 0x2A6, 2225479, 0x2A6, "Pip-Boy opened (cursor constraint)"sv },
			CallSite{ 188351, 0x3AC, 2224616, 0x371, "Terminal holotape render-target dimensions"sv },
			CallSite{ 926335, 0x860, 2224614, 0x7E3, "Terminal holotape screen-space hit testing"sv },
		};

		constexpr std::array kOpenAudioSites{
			CallSite{ 1477369, 0xE6, 2225479, 0xEC, "Pip-Boy opened (power-armor open sound)"sv },
		};

		constexpr std::array kCloseAudioSites{
			CallSite{ 731410, 0xEF, 2225480, 0x12B, "ClosedownPipboy (power-armor close sound)"sv },
		};

		constexpr std::array kPipboyLightSites{
			CallSite{ 1477369, 0x313, 2225479, 0x313, "Pip-Boy opened (keep Pip-Boy light on)"sv },
		};

		// These are every direct caller of OnPipboyClosed in each supported family.
		// This is an authoritative post-close seam without a function-prologue detour.
		constexpr std::array kPipboyClosedSites{
			CallSite{ 1231000, 0x1D3, 2225457, 0x1ED, "OnPipboyCloseAnim -> OnPipboyClosed"sv },
			CallSite{ 261739, 0x81, 2225493, 0x78, "PipboyManager event handler -> OnPipboyClosed"sv },
		};

		constexpr std::array kPipboyOpenSites{
			CallSite{ 181358, 0x23F, 2249426, 0x32F, "Tab handler -> PlayPipboyOpenAnim"sv },
			CallSite{ 943894, 0x134, 2219795, 0x134, "companion use-item -> PlayPipboyOpenAnim"sv },
		};

		constexpr std::array kPipboyCloseSites{
			CallSite{ 273179, 0x3A, 2231392, 0x232, "PlayPipboyCloseAnim caller 1"sv },
			CallSite{ 643948, 0x232, 2223217, 0x3F, "PlayPipboyCloseAnim caller 2"sv },
			CallSite{ 643948, 0x245, 2231408, 0x105, "PlayPipboyCloseAnim caller 3"sv },
			CallSite{ 926335, 0x3E8, 2224181, 0x21D, "PlayPipboyCloseAnim caller 4"sv },
			CallSite{ 1299608, 0x48, 2224181, 0x230, "PlayPipboyCloseAnim caller 5"sv },
			CallSite{ 301794, 0x105, 2224614, 0x346, "PlayPipboyCloseAnim caller 6"sv },
		};

		constexpr std::array kPipboyLoadHolotapeSitesOG{
			CallSite{ 634650, 0x9E, 0, 0, "animated holotape load caller 1"sv },
			CallSite{ 1411297, 0x1BD, 0, 0, "animated holotape load caller 2"sv },
		};

		constexpr std::array kPipboyLoadHolotapeSitesAE{
			CallSite{ 0, 0, 2224162, 0x147, "animated holotape load caller 1"sv },
			CallSite{ 0, 0, 2224169, 0x72, "animated holotape load caller 2"sv },
			CallSite{ 0, 0, 2234886, 0x1B3, "animated holotape load caller 3"sv },
		};

		[[nodiscard]] Family GetFamily(const REL::Version& a_runtime)
		{
			return a_runtime == F4SE::RUNTIME_1_10_163 ? Family::kOG : Family::kAE;
		}

		[[nodiscard]] std::uintptr_t ResolveID(
			const Family a_family,
			const std::uint64_t a_ogID,
			const std::uint64_t a_aeID)
		{
			return REL::ID(a_family == Family::kOG ? a_ogID : a_aeID).address();
		}

		[[nodiscard]] std::uintptr_t ResolveSite(
			const Family a_family,
			const CallSite& a_site)
		{
			return ResolveID(a_family, a_site.ogID, a_site.aeID) +
			       (a_family == Family::kOG ? a_site.ogOffset : a_site.aeOffset);
		}

		[[nodiscard]] std::uintptr_t GetCallTarget(const std::uintptr_t a_callAddress)
		{
			const auto displacement = *reinterpret_cast<const std::int32_t*>(a_callAddress + 1);
			return a_callAddress + 5 + displacement;
		}

		[[nodiscard]] bool ResolveSites(
			const Family a_family,
			const std::span<const CallSite> a_sites,
			const std::uintptr_t a_expectedTarget,
			std::vector<std::uintptr_t>& a_out)
		{
			for (const auto& site : a_sites) {
				const auto address = ResolveSite(a_family, site);
				if (*reinterpret_cast<const std::uint8_t*>(address) != 0xE8) {
					REX::ERROR("Expected CALL at 0x{:X} for {}; refusing to patch", address, site.desc);
					return false;
				}
				if (GetCallTarget(address) != a_expectedTarget) {
					REX::ERROR("Unexpected call target at 0x{:X} for {}; refusing to patch", address, site.desc);
					return false;
				}
				a_out.push_back(address);
			}
			return true;
		}

		[[nodiscard]] bool ValidateVfunc(
			const std::uintptr_t a_vtable,
			const std::size_t a_index,
			const std::uintptr_t a_expected,
			const std::string_view a_desc)
		{
			const auto* entries = reinterpret_cast<const std::uintptr_t*>(a_vtable);
			if (entries[a_index] != a_expected) {
				REX::ERROR("Unexpected {} vtable entry; refusing to patch", a_desc);
				return false;
			}
			return true;
		}
	}

	bool IsSupported(const REL::Version& a_runtime)
	{
		return a_runtime == F4SE::RUNTIME_1_10_163 ||
		       a_runtime >= F4SE::RUNTIME_1_11_221;
	}

	std::optional<HookAddresses> ResolveHookAddresses(const REL::Version& a_runtime)
	{
		if (!IsSupported(a_runtime)) {
			return std::nullopt;
		}

		const auto family = GetFamily(a_runtime);
		HookAddresses addresses{};
		addresses.actorInPowerArmor = RE::ID::PowerArmor::ActorInPowerArmor.address();

		if (!ResolveSites(family, kPresentationSites, addresses.actorInPowerArmor, addresses.presentation) ||
			!ResolveSites(family, kOpenAudioSites, addresses.actorInPowerArmor, addresses.openAudio) ||
			!ResolveSites(family, kCloseAudioSites, addresses.actorInPowerArmor, addresses.closeAudio) ||
			!ResolveSites(family, kPipboyLightSites, addresses.actorInPowerArmor, addresses.light)) {
			return std::nullopt;
		}

		const auto onPipboyClosed = RE::ID::PipboyManager::OnPipboyClosed.address();
		if (!ResolveSites(family, kPipboyClosedSites, onPipboyClosed, addresses.pipboyClosedCalls)) {
			return std::nullopt;
		}

		std::vector<std::uintptr_t> openCalls;
		if (!ResolveSites(family, kPipboyOpenSites, RE::ID::PipboyManager::PlayPipboyOpenAnim.address(), openCalls)) {
			return std::nullopt;
		}
		addresses.tabHandlerCall = openCalls[0];
		addresses.companionUseItemCall = openCalls[1];

		if (!ResolveSites(family, kPipboyCloseSites, RE::ID::PipboyManager::PlayPipboyCloseAnim.address(), addresses.pipboyCloseCalls)) {
			return std::nullopt;
		}
		const auto holotapeSites = family == Family::kOG ?
			std::span<const CallSite>{ kPipboyLoadHolotapeSitesOG } :
			std::span<const CallSite>{ kPipboyLoadHolotapeSitesAE };
		if (!ResolveSites(family, holotapeSites, RE::ID::PipboyManager::PlayPipboyLoadHolotapeAnim.address(), addresses.pipboyLoadHolotapeCalls)) {
			return std::nullopt;
		}

		addresses.pipboyMenuVtable = RE::PipboyMenu::VTABLE[0].address();
		if (!ValidateVfunc(addresses.pipboyMenuVtable, kPipboyMenuAdvanceMovieIndex,
			ResolveID(family, 561368, 2224183), "PipboyMenu AdvanceMovie"sv)) {
			return std::nullopt;
		}

		addresses.pipboyMenuInputVtable = REL::ID(85678).address();
		if (!ValidateVfunc(addresses.pipboyMenuInputVtable, kShouldHandleEventIndex,
			ResolveID(family, 607291, 2224188), "PipboyMenu ShouldHandleEvent"sv) ||
			!ValidateVfunc(addresses.pipboyMenuInputVtable, kOnButtonEventIndex,
				ResolveID(family, 75248, 2224189), "PipboyMenu OnButtonEvent"sv)) {
			return std::nullopt;
		}

		addresses.firstPersonStateVtable = REL::ID(246953).address();
		if (!ValidateVfunc(addresses.firstPersonStateVtable, kFirstPersonStateUpdateIndex,
			ResolveID(family, 1392051, 2248260), "FirstPersonState Update"sv)) {
			return std::nullopt;
		}

		HookAddresses::PipboyActiveSetter activeSetter{};
		activeSetter.address = ResolveID(family, 318434, 2225492);
		activeSetter.abi = family == Family::kOG ?
			HookAddresses::PipboyActiveSetter::ABI::kValueEventSource :
			HookAddresses::PipboyActiveSetter::ABI::kPipboyManager;
		addresses.setPipboyActive = activeSetter;

		HookAddresses::Rain rain{};
		rain.hudMenuVtable = RE::HUDMenu::VTABLE[0].address();
		if (!ValidateVfunc(rain.hudMenuVtable, kHUDMenuAdvanceMovieIndex,
			ResolveID(family, 494688, 2248853), "HUDMenu AdvanceMovie"sv)) {
			return std::nullopt;
		}
		rain.powerArmorHUDRainModifierGetter = ResolveID(family, 547006, 2199993);
		rain.referenceIsInterior = ResolveID(family, 1108031, 2201166);
		rain.getSubmergeLevel = ResolveID(family, 44316, 2229864);
		addresses.rain = rain;

		REX::INFO("Validated complete {} hook manifest for Fallout 4 {}",
			family == Family::kOG ? "OG" : "AE", a_runtime.string());
		return addresses;
	}
}
