#include "Runtime.h"

namespace PowerArmorPipBoyUI::Runtime
{
	namespace
	{
		struct CallSite
		{
			std::uint64_t id;
			std::ptrdiff_t offset;
			std::string_view desc;
		};

		// Each entry is a call to Actor::IsInPowerArmor in the Pip-Boy presentation path.
		// Forcing them true selects the power-armor presentation: the same PipboyMenu movie,
		// composited onto a camera-attached quad instead of the player's wrist screen. The
		// PipboyMenu hit-test site is essential as well: rendering and input must use the
		// same geometry and coordinate transform.
		constexpr std::array kPresentationSites{
			CallSite{ 394568, 0x30, "AddMenuToPipboy (viewport rect)"sv },
			CallSite{ 1444875, 0x46, "LowerPipboy (skip lower anim)"sv },
			CallSite{ 726763, 0x50, "RaisePipboy (cursor constraint)"sv },
			CallSite{ 726763, 0xC6, "RaisePipboy (skip raise anim)"sv },
			CallSite{ 273927, 0x87, "PlayPipboyCloseAnim"sv },
			CallSite{ 302903, 0x19, "ProcessLoweringReason"sv },
			CallSite{ 302903, 0x29, "ProcessLoweringReason"sv },
			CallSite{ 900802, 0x1F, "UpdateCursorConstraint"sv },
			CallSite{ 643948, 0x3BD, "PipboyMenu screen-space hit testing"sv },

			// The sites that decide what the menu is actually drawn on. Without these the
			// Interface3D renderer stays world-attached to a wrist that never rises and the
			// screen shader keeps its wrist constants, which is why the menu came up blank.
			//
			// These IDs were read out of version-1-10-163-0.bin rather than taken from
			// CommonLibF4's IDs.h, whose names do not line up here: its
			// RefreshPipboyRenderSurface{81339} is 0xC20A20 (the non-power-armor helper),
			// and its OnPipboyOpened{1299608} is 0xC1F590. The render-surface switch is
			// 0xC21240 and the opened handler is 0xC20B00.
			CallSite{ 157921, 0x19, "render-surface setup (screen-attached quad)"sv },
			CallSite{ 1477369, 0x154, "Pip-Boy opened (item preview camera)"sv },
			CallSite{ 1477369, 0x24C, "Pip-Boy opened (screen shader + TAA mode)"sv },
			CallSite{ 1477369, 0x2A6, "Pip-Boy opened (cursor constraint)"sv },

			// Terminal-form holotapes use TerminalMenu instead of PipboyHolotapeMenu.
			// Its render target and mouse projection each make their own PA decision.
			CallSite{ 188351, 0x3AC, "Terminal holotape render-target dimensions"sv },
			CallSite{ 926335, 0x860, "Terminal holotape screen-space hit testing"sv },
		};

		constexpr std::array kOpenAudioSites{
			CallSite{ 1477369, 0xE6, "Pip-Boy opened (power-armor open sound)"sv },
		};

		constexpr std::array kCloseAudioSites{
			CallSite{ 731410, 0xEF, "ClosedownPipboy (power-armor close sound)"sv },
		};

		// In power armor the game leaves the Pip-Boy light alone; outside it, the light is
		// switched off while the menu is up and restored on close. Vanilla behaviour is the
		// less surprising default, so this one is opt-in.
		constexpr std::array kPipboyLightSites{
			CallSite{ 1477369, 0x313, "Pip-Boy opened (keep Pip-Boy light on)"sv },
		};

		// OnPipboyCloseAnim has the only executable reference to ClosedownPipboy in
		// 1.10.163. Hooking that validated call gives every engine-driven final close
		// an authoritative post-close cleanup point without detouring a function prologue.
		constexpr std::array kPipboyClosedownSites{
			CallSite{ 592088, 0x12, "OnPipboyCloseAnim (final ClosedownPipboy)"sv },
		};

		// Every direct 1.10.163 caller of PipboyManager::PlayPipboyCloseAnim. Calls
		// that request an animated close must be completed synchronously for our
		// screen-only presentation: the non-PA player graph cannot produce the PA
		// close event. Intercepting all callers also covers deferred closes such as
		// map fast travel; the input-vtable hook alone sees only keyboard/controller
		// button events.
		constexpr std::array kPipboyCloseCallOffsets{
			0xB334FA,
			0xB937F2,
			0xB93805,
			0xBC8148,
			0xC1F5D8,
			0xE1D205,
		};

		// The two 1.10.163 calls that request an animated holotape load. A third direct
		// reference at 0xB90BD2 is already the engine's a_noAnim=true tail-call and does
		// not need interception. Forced presentation uses that same native path.
		constexpr std::array kPipboyLoadHolotapeCallOffsets{
			0xB90BFE,
			0xF49F3D,
		};

		[[nodiscard]] std::uintptr_t GetCallTarget(const std::uintptr_t a_callAddress)
		{
			const auto displacement = *reinterpret_cast<const std::int32_t*>(a_callAddress + 1);
			return a_callAddress + 5 + displacement;
		}

		[[nodiscard]] bool ResolveSites(
			std::span<const CallSite> a_sites,
			const std::uintptr_t a_expectedTarget,
			std::vector<std::uintptr_t>& a_out)
		{
			for (const auto& site : a_sites) {
				const auto address = REL::ID(site.id).address() + site.offset;

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
	}

	std::optional<HookAddresses> ResolveHookAddresses()
	{
		HookAddresses addresses{};

		// The OG ID is confirmed by the 1.10.163 Address Library and the BGS
		// byte-signature corpus. The post-NG equivalent is ID 2219437.
		addresses.actorInPowerArmor = REL::ID(1176757).address();

		addresses.presentation.reserve(kPresentationSites.size());
		if (!ResolveSites(kPresentationSites, addresses.actorInPowerArmor, addresses.presentation)) {
			return std::nullopt;
		}

		if (!ResolveSites(kOpenAudioSites, addresses.actorInPowerArmor, addresses.openAudio)) {
			return std::nullopt;
		}
		if (!ResolveSites(kCloseAudioSites, addresses.actorInPowerArmor, addresses.closeAudio)) {
			return std::nullopt;
		}
		if (!ResolveSites(kPipboyLightSites, addresses.actorInPowerArmor, addresses.light)) {
			return std::nullopt;
		}

		const auto closedownPipboyAddress = REL::ID(731410).address();
		addresses.closedown.reserve(kPipboyClosedownSites.size());
		if (!ResolveSites(kPipboyClosedownSites, closedownPipboyAddress, addresses.closedown)) {
			return std::nullopt;
		}

		// The Tab handler calls PlayPipboyOpenAnim directly. Redirect it through the
		// generic no-animation path used by immediate menu opens.
		addresses.tabHandlerCall = REL::ID(181358).address() + 0x23F;
		addresses.companionUseItemCall = REL::Offset(0x9FC974).address();
		const auto playPipboyOpenAnimAddress = REL::ID(663900).address();
		const auto playPipboyCloseAnimAddress = REL::ID(273927).address();
		const auto playPipboyLoadHolotapeAnimAddress = REL::ID(477096).address();
		if (*reinterpret_cast<const std::uint8_t*>(addresses.tabHandlerCall) != 0xE8 ||
			GetCallTarget(addresses.tabHandlerCall) != playPipboyOpenAnimAddress) {
			REX::ERROR(
				"Unexpected Tab-handler call at 0x{:X}; refusing to patch",
				addresses.tabHandlerCall);
			return std::nullopt;
		}
		if (*reinterpret_cast<const std::uint8_t*>(addresses.companionUseItemCall) != 0xE8 ||
			GetCallTarget(addresses.companionUseItemCall) != playPipboyOpenAnimAddress) {
			REX::ERROR(
				"Unexpected companion use-item call at 0x{:X}; refusing to patch",
				addresses.companionUseItemCall);
			return std::nullopt;
		}

		addresses.pipboyCloseCalls.reserve(kPipboyCloseCallOffsets.size());
		for (const auto offset : kPipboyCloseCallOffsets) {
			const auto address = REL::Offset(offset).address();
			if (*reinterpret_cast<const std::uint8_t*>(address) != 0xE8 ||
				GetCallTarget(address) != playPipboyCloseAnimAddress) {
				REX::ERROR("Unexpected PlayPipboyCloseAnim call at 0x{:X}; refusing to patch", address);
				return std::nullopt;
			}
			addresses.pipboyCloseCalls.push_back(address);
		}

		addresses.pipboyLoadHolotapeCalls.reserve(kPipboyLoadHolotapeCallOffsets.size());
		for (const auto offset : kPipboyLoadHolotapeCallOffsets) {
			const auto address = REL::Offset(offset).address();
			if (*reinterpret_cast<const std::uint8_t*>(address) != 0xE8 ||
				GetCallTarget(address) != playPipboyLoadHolotapeAnimAddress) {
				REX::ERROR(
					"Unexpected PlayPipboyLoadHolotapeAnim call at 0x{:X}; refusing to patch",
					address);
				return std::nullopt;
			}
			addresses.pipboyLoadHolotapeCalls.push_back(address);
		}

		// PipboyMenu's secondary BSInputEventUser vtable. Hook the two input methods
		// needed to provide a native toggle-to-close fallback for forced opens. The
		// exact entries and function IDs are validated before any hooks are written.
		addresses.pipboyMenuInputVtable = REL::ID(85678).address();
		const auto* pipboyMenuEntries = reinterpret_cast<const std::uintptr_t*>(
			addresses.pipboyMenuInputVtable);
		if (pipboyMenuEntries[kShouldHandleEventIndex] != REL::ID(607291).address() ||
			pipboyMenuEntries[kOnButtonEventIndex] != REL::ID(75248).address()) {
			REX::ERROR("Unexpected PipboyMenu input vtable; refusing to patch");
			return std::nullopt;
		}

		// FirstPersonState::Update is vfunc 0x0B after BSInputEventUser's entries.
		// It is the point where the camera resamples the first-person graph and
		// advances locomotion interpolation. Hooking it lets a forced menu hold the
		// current view without changing PlayerCamera's state stack.
		addresses.firstPersonStateVtable = REL::ID(246953).address();
		const auto* firstPersonStateEntries = reinterpret_cast<const std::uintptr_t*>(
			addresses.firstPersonStateVtable);
		if (firstPersonStateEntries[kFirstPersonStateUpdateIndex] !=
			REL::Offset(0x1243220).address()) {
			REX::ERROR("Unexpected FirstPersonState vtable; refusing to patch");
			return std::nullopt;
		}

		return addresses;
	}
}
