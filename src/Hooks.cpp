#include "Hooks.h"

#include "Presentation.h"
#include "RainOverlay.h"
#include "Runtime.h"

namespace PowerArmorPipBoyUI::Hooks
{
	namespace
	{
		using ActorInPowerArmor_t = bool (*)(const RE::Actor&);
		using OnPipboyClosed_t = void (*)(RE::PipboyManager*);
		using SetPipboyActiveOG_t = bool (*)(
			RE::BSTValueEventSource<RE::IsPipboyActiveEvent>*,
			const bool*);
		using SetPipboyActiveAE_t = void (*)(RE::PipboyManager*, bool);
		using PipboyMenuShouldHandleEvent_t = bool (*)(
			RE::BSInputEventUser*,
			const RE::InputEvent*);
		using PipboyMenuOnButtonEvent_t = void (*)(
			RE::BSInputEventUser*,
			const RE::ButtonEvent*);
		using FirstPersonStateUpdate_t = void (*)(
			RE::TESCameraState*,
			RE::BSTSmartPointer<RE::TESCameraState>&);
		using PipboyMenuAdvanceMovie_t = void (*)(RE::IMenu*, float, std::uint64_t);
		using GetPowerArmorHUDRainModifier_t = RE::TESImageSpaceModifier* (*)();
		using ReferenceIsInterior_t = bool (*)(const RE::TESObjectREFR*);
		using GetSubmergeLevel_t = float (*)(
			const RE::TESObjectREFR*,
			const RE::NiPoint3*,
			RE::TESObjectCELL*);

		ActorInPowerArmor_t g_actorInPowerArmor = nullptr;
		OnPipboyClosed_t g_onPipboyClosed = nullptr;
		SetPipboyActiveOG_t g_setPipboyActiveOG = nullptr;
		SetPipboyActiveAE_t g_setPipboyActiveAE = nullptr;
		PipboyMenuShouldHandleEvent_t g_pipboyMenuShouldHandleEvent = nullptr;
		PipboyMenuOnButtonEvent_t g_pipboyMenuOnButtonEvent = nullptr;
		FirstPersonStateUpdate_t g_firstPersonStateUpdate = nullptr;
		PipboyMenuAdvanceMovie_t g_pipboyMenuAdvanceMovie = nullptr;
		PipboyMenuAdvanceMovie_t g_hudMenuAdvanceMovie = nullptr;
		GetPowerArmorHUDRainModifier_t g_getPowerArmorHUDRainModifier = nullptr;
		ReferenceIsInterior_t g_referenceIsInterior = nullptr;
		GetSubmergeLevel_t g_getSubmergeLevel = nullptr;
	}

	void Install(const Runtime::HookAddresses& a_addresses)
	{
		g_actorInPowerArmor = reinterpret_cast<ActorInPowerArmor_t>(
			a_addresses.actorInPowerArmor);
		if (a_addresses.setPipboyActive) {
			switch (a_addresses.setPipboyActive->abi) {
			case Runtime::HookAddresses::PipboyActiveSetter::ABI::kValueEventSource:
				g_setPipboyActiveOG = reinterpret_cast<SetPipboyActiveOG_t>(
					a_addresses.setPipboyActive->address);
				break;
			case Runtime::HookAddresses::PipboyActiveSetter::ABI::kPipboyManager:
				g_setPipboyActiveAE = reinterpret_cast<SetPipboyActiveAE_t>(
					a_addresses.setPipboyActive->address);
				break;
			}
		}
		if (a_addresses.rain) {
			g_getPowerArmorHUDRainModifier =
				reinterpret_cast<GetPowerArmorHUDRainModifier_t>(
					a_addresses.rain->powerArmorHUDRainModifierGetter);
			g_referenceIsInterior = reinterpret_cast<ReferenceIsInterior_t>(
				a_addresses.rain->referenceIsInterior);
			g_getSubmergeLevel = reinterpret_cast<GetSubmergeLevel_t>(
				a_addresses.rain->getSubmergeLevel);
		}

		auto& trampoline = REL::GetTrampoline();
		for (const auto address : a_addresses.presentation) {
			trampoline.write_call<5>(address, Presentation::UsePowerArmorPipboy);
		}
		for (const auto address : a_addresses.openAudio) {
			trampoline.write_call<5>(address, Presentation::UsePowerArmorPipboyAudio);
		}
		for (const auto address : a_addresses.closeAudio) {
			trampoline.write_call<5>(address, Presentation::UsePowerArmorPipboyAudio);
		}
		for (const auto address : a_addresses.light) {
			trampoline.write_call<5>(address, Presentation::UsePowerArmorPipboyLight);
		}

		trampoline.write_call<5>(
			a_addresses.tabHandlerCall,
			Presentation::OpenPipboyWithoutWristAnimation);
		trampoline.write_call<5>(
			a_addresses.companionUseItemCall,
			Presentation::OpenPipboyWithoutWristAnimation);

		for (const auto address : a_addresses.pipboyCloseCalls) {
			trampoline.write_call<5>(
				address,
				Presentation::PlayPipboyCloseForForcedPresentation);
		}
		for (const auto address : a_addresses.pipboyLoadHolotapeCalls) {
			trampoline.write_call<5>(
				address,
				Presentation::PlayPipboyLoadHolotapeForForcedPresentation);
		}

		for (const auto address : a_addresses.pipboyClosedCalls) {
			const auto original = reinterpret_cast<OnPipboyClosed_t>(
				trampoline.write_call<5>(address, Presentation::OnPipboyClosedAndReset));
			if (!g_onPipboyClosed) {
				g_onPipboyClosed = original;
			} else if (g_onPipboyClosed != original) {
				REX::ERROR("OnPipboyClosed callers did not share the validated target");
			}
		}

		REL::Relocation<std::uintptr_t> pipboyMenuInputVtable{
			a_addresses.pipboyMenuInputVtable
		};
		g_pipboyMenuShouldHandleEvent = reinterpret_cast<PipboyMenuShouldHandleEvent_t>(
			pipboyMenuInputVtable.write_vfunc(
				Runtime::kShouldHandleEventIndex,
				Presentation::ShouldHandleForcedPipboyClose));
		g_pipboyMenuOnButtonEvent = reinterpret_cast<PipboyMenuOnButtonEvent_t>(
			pipboyMenuInputVtable.write_vfunc(
				Runtime::kOnButtonEventIndex,
				Presentation::HandleForcedPipboyClose));

		REL::Relocation<std::uintptr_t> firstPersonStateVtable{
			a_addresses.firstPersonStateVtable
		};
		g_firstPersonStateUpdate = reinterpret_cast<FirstPersonStateUpdate_t>(
			firstPersonStateVtable.write_vfunc(
				Runtime::kFirstPersonStateUpdateIndex,
				Presentation::UpdateFirstPersonCameraForForcedPresentation));

		// CommonLib's primary PipboyMenu vtable ID is Address Library-backed for
		// every supported runtime. AdvanceMovie provides a genuine menu/render frame
		// boundary without adding another executable call-site address.
		REL::Relocation<std::uintptr_t> pipboyMenuVtable{ a_addresses.pipboyMenuVtable };
		g_pipboyMenuAdvanceMovie = reinterpret_cast<PipboyMenuAdvanceMovie_t>(
			pipboyMenuVtable.write_vfunc(
				Runtime::kPipboyMenuAdvanceMovieIndex,
				Presentation::AdvancePipboyMenuForTerminalReturn));

		if (a_addresses.rain) {
			REL::Relocation<std::uintptr_t> hudMenuVtable{ a_addresses.rain->hudMenuVtable };
			g_hudMenuAdvanceMovie = reinterpret_cast<PipboyMenuAdvanceMovie_t>(
				hudMenuVtable.write_vfunc(
					Runtime::kHUDMenuAdvanceMovieIndex,
					RainOverlay::AdvanceHUDMenu));
		}

		REX::INFO(
			"Installed {} presentation hooks, 2 no-animation open overrides, {} no-animation holotape overrides, {} close overrides, {} authoritative close cleanup hooks, the forced-close input fallback, the first-person camera freeze, the PipboyMenu frame handoff, and rain={}",
			a_addresses.presentation.size(),
			a_addresses.pipboyLoadHolotapeCalls.size(),
			a_addresses.pipboyCloseCalls.size(),
			a_addresses.pipboyClosedCalls.size(),
			a_addresses.rain.has_value());
	}

	bool ActorInPowerArmor(const RE::Actor& a_actor)
	{
		return g_actorInPowerArmor(a_actor);
	}

	void OnPipboyClosed(RE::PipboyManager* a_manager)
	{
		g_onPipboyClosed(a_manager);
	}

	bool SetPipboyActive(RE::PipboyManager* a_manager, const bool a_active)
	{
		if (g_setPipboyActiveOG) {
			g_setPipboyActiveOG(
				std::addressof(a_manager->pipboyActive),
				std::addressof(a_active));
			return true;
		}
		if (g_setPipboyActiveAE) {
			g_setPipboyActiveAE(a_manager, a_active);
			return true;
		}
		return false;
	}

	bool PipboyMenuShouldHandleEvent(
		RE::BSInputEventUser* a_inputUser,
		const RE::InputEvent* a_event)
	{
		return g_pipboyMenuShouldHandleEvent(a_inputUser, a_event);
	}

	void PipboyMenuOnButtonEvent(
		RE::BSInputEventUser* a_inputUser,
		const RE::ButtonEvent* a_event)
	{
		g_pipboyMenuOnButtonEvent(a_inputUser, a_event);
	}

	void FirstPersonStateUpdate(
		RE::TESCameraState* a_state,
		RE::BSTSmartPointer<RE::TESCameraState>& a_nextState)
	{
		g_firstPersonStateUpdate(a_state, a_nextState);
	}

	void PipboyMenuAdvanceMovie(
		RE::IMenu* a_menu,
		const float a_timeDelta,
		const std::uint64_t a_time)
	{
		g_pipboyMenuAdvanceMovie(a_menu, a_timeDelta, a_time);
	}

	void HUDMenuAdvanceMovie(
		RE::IMenu* a_menu,
		const float a_timeDelta,
		const std::uint64_t a_time)
	{
		g_hudMenuAdvanceMovie(a_menu, a_timeDelta, a_time);
	}

	RE::TESImageSpaceModifier* GetPowerArmorHUDRainModifier()
	{
		return g_getPowerArmorHUDRainModifier();
	}

	bool ReferenceIsInterior(const RE::TESObjectREFR& a_reference)
	{
		return g_referenceIsInterior(std::addressof(a_reference));
	}

	float GetSubmergeLevel(const RE::TESObjectREFR& a_reference)
	{
		return g_getSubmergeLevel(
			std::addressof(a_reference),
			std::addressof(a_reference.data.location),
			a_reference.parentCell);
	}
}
