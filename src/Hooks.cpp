#include "Hooks.h"

#include "Presentation.h"
#include "Runtime.h"

namespace PowerArmorPipBoyUI::Hooks
{
	namespace
	{
		using ActorInPowerArmor_t = bool (*)(const RE::Actor&);
		using ClosedownPipboy_t = void (*)(RE::PipboyManager*);
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

		ActorInPowerArmor_t g_actorInPowerArmor = nullptr;
		ClosedownPipboy_t g_closedownPipboy = nullptr;
		PipboyMenuShouldHandleEvent_t g_pipboyMenuShouldHandleEvent = nullptr;
		PipboyMenuOnButtonEvent_t g_pipboyMenuOnButtonEvent = nullptr;
		FirstPersonStateUpdate_t g_firstPersonStateUpdate = nullptr;
		PipboyMenuAdvanceMovie_t g_pipboyMenuAdvanceMovie = nullptr;
	}

	bool Install()
	{
		const auto addresses = Runtime::ResolveHookAddresses();
		if (!addresses) {
			return false;
		}

		g_actorInPowerArmor = reinterpret_cast<ActorInPowerArmor_t>(
			addresses->actorInPowerArmor);

		auto& trampoline = REL::GetTrampoline();
		for (const auto address : addresses->presentation) {
			trampoline.write_call<5>(address, Presentation::UsePowerArmorPipboy);
		}
		for (const auto address : addresses->openAudio) {
			trampoline.write_call<5>(address, Presentation::UsePowerArmorPipboyAudio);
		}
		for (const auto address : addresses->closeAudio) {
			trampoline.write_call<5>(address, Presentation::UsePowerArmorPipboyAudio);
		}
		for (const auto address : addresses->light) {
			trampoline.write_call<5>(address, Presentation::UsePowerArmorPipboyLight);
		}

		trampoline.write_call<5>(
			addresses->tabHandlerCall,
			Presentation::OpenPipboyWithoutWristAnimation);
		trampoline.write_call<5>(
			addresses->companionUseItemCall,
			Presentation::OpenPipboyWithoutWristAnimation);

		for (const auto address : addresses->pipboyCloseCalls) {
			trampoline.write_call<5>(
				address,
				Presentation::PlayPipboyCloseForForcedPresentation);
		}
		for (const auto address : addresses->pipboyLoadHolotapeCalls) {
			trampoline.write_call<5>(
				address,
				Presentation::PlayPipboyLoadHolotapeForForcedPresentation);
		}

		g_closedownPipboy = reinterpret_cast<ClosedownPipboy_t>(
			trampoline.write_call<5>(
				addresses->closedown.front(),
				Presentation::ClosedownPipboyAndReset));

		REL::Relocation<std::uintptr_t> pipboyMenuInputVtable{
			addresses->pipboyMenuInputVtable
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
			addresses->firstPersonStateVtable
		};
		g_firstPersonStateUpdate = reinterpret_cast<FirstPersonStateUpdate_t>(
			firstPersonStateVtable.write_vfunc(
				Runtime::kFirstPersonStateUpdateIndex,
				Presentation::UpdateFirstPersonCameraForForcedPresentation));

		// CommonLib's primary PipboyMenu vtable ID is Address Library-backed for
		// every supported runtime. AdvanceMovie provides a genuine menu/render frame
		// boundary without adding another executable call-site address.
		REL::Relocation<std::uintptr_t> pipboyMenuVtable{ RE::PipboyMenu::VTABLE[0] };
		g_pipboyMenuAdvanceMovie = reinterpret_cast<PipboyMenuAdvanceMovie_t>(
			pipboyMenuVtable.write_vfunc(
				0x04,
				Presentation::AdvancePipboyMenuForTerminalReturn));

		REX::INFO(
			"Installed {} presentation hooks, 2 no-animation open overrides, {} no-animation holotape overrides, {} close overrides, authoritative closedown cleanup, the nested-menu-aware forced-close input fallback, the first-person camera freeze, and the PipboyMenu frame handoff",
			addresses->presentation.size(),
			addresses->pipboyLoadHolotapeCalls.size(),
			addresses->pipboyCloseCalls.size());
		return true;
	}

	bool ActorInPowerArmor(const RE::Actor& a_actor)
	{
		return g_actorInPowerArmor(a_actor);
	}

	void ClosedownPipboy(RE::PipboyManager* a_manager)
	{
		g_closedownPipboy(a_manager);
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
}
