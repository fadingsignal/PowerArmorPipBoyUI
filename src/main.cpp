#include "version.h"

namespace
{
	using ActorInPowerArmor_t = bool (*)(const RE::Actor&);
	using PlayPipboyOpenAnim_t = void (*)(RE::PipboyManager*, const RE::BSFixedString&);
	using PlayPipboyGenericOpenAnim_t = void (*)(
		RE::PipboyManager*,
		const RE::BSFixedString&,
		const RE::BSFixedString&,
		bool);
	using PipboyMenuShouldHandleEvent_t = bool (*)(RE::BSInputEventUser*, const RE::InputEvent*);
	using PipboyMenuOnButtonEvent_t = void (*)(RE::BSInputEventUser*, const RE::ButtonEvent*);
	using SetPipboyActive_t = bool (*)(
		RE::BSTValueEventSource<RE::IsPipboyActiveEvent>*,
		const bool*);

	struct CallSite
	{
		std::uint64_t    id;
		std::ptrdiff_t   offset;
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

	ActorInPowerArmor_t g_actorInPowerArmor = nullptr;
	PlayPipboyOpenAnim_t g_playPipboyOpenAnim = nullptr;
	PipboyMenuShouldHandleEvent_t g_pipboyMenuShouldHandleEvent = nullptr;
	PipboyMenuOnButtonEvent_t g_pipboyMenuOnButtonEvent = nullptr;
	RE::NiPointer<RE::NiNode> g_powerArmorPipboyScreen;
	std::atomic_bool g_forceThisOpen = false;
	bool g_forcePowerArmorPipboy = true;
	bool g_powerArmorAudio = false;
	bool g_keepPipboyLightOn = false;

	bool SetPipboyActive(RE::PipboyManager* a_manager, const bool a_active)
	{
		// BSTValueEventSource's engine setter locks the value and broadcasts an
		// IsPipboyActiveEvent when it changes. This is the same function called by
		// PipboyManager::OnPipboyOpened/Closed in Fallout 4 1.10.163.
		static REL::Relocation<SetPipboyActive_t> setActive{ REL::ID(318434) };
		return setActive(std::addressof(a_manager->pipboyActive), std::addressof(a_active));
	}

	[[nodiscard]] std::filesystem::path GetIniPath()
	{
		std::array<wchar_t, REX::W32::MAX_PATH> executablePath{};
		const auto length = REX::W32::GetModuleFileNameW(
			nullptr,
			executablePath.data(),
			static_cast<std::uint32_t>(executablePath.size()));

		if (length == 0 || length >= executablePath.size()) {
			return std::filesystem::current_path() / "Data/F4SE/Plugins/PowerArmorPipBoyUI.ini";
		}

		return std::filesystem::path(executablePath.data()).parent_path() /
		       "Data/F4SE/Plugins/PowerArmorPipBoyUI.ini";
	}

	[[nodiscard]] bool GetSetting(const std::filesystem::path& a_iniPath, const wchar_t* a_key, bool a_default)
	{
		return REX::W32::GetPrivateProfileIntW(
			L"General",
			a_key,
			a_default ? 1 : 0,
			a_iniPath.c_str()) != 0;
	}

	void LoadSettings()
	{
		const auto iniPath = GetIniPath();
		g_forcePowerArmorPipboy = GetSetting(iniPath, L"bForcePowerArmorPipboy", true);
		g_powerArmorAudio = GetSetting(iniPath, L"bPowerArmorAudio", false);
		g_keepPipboyLightOn = GetSetting(iniPath, L"bKeepPipboyLightOn", false);

		REX::INFO(
			"bForcePowerArmorPipboy={} bPowerArmorAudio={} bKeepPipboyLightOn={} ({})",
			g_forcePowerArmorPipboy,
			g_powerArmorAudio,
			g_keepPipboyLightOn,
			iniPath.string());
	}

	// The PA presentation needs only this screen quad. PowerArmorGeometry normally loads
	// it together with the dashboard and rain geometry after a PreloadPowerArmor event.
	// Loading just the quad avoids invoking that subsystem's completion callback out of
	// sequence and avoids creating any of the PowerArmorHUDMenu geometry.
	[[nodiscard]] bool LoadPowerArmorPipboyScreen()
	{
		if (g_powerArmorPipboyScreen) {
			return true;
		}

		RE::BSModelDB::DBTraits::ArgsType args{};
		args.lodFadeMult = RE::ENUM_LOD_MULT::kNone;
		args.loadLevel = 0;
		args.prepareAfterLoad = true;
		args.faceGenModel = false;
		args.useErrorMarker = true;
		args.performProcess = true;
		args.createFadeNode = true;
		args.loadTextures = true;

		const auto result = RE::BSModelDB::Demand(
			"Interface/Objects/PADashPipboyScreen.nif",
			std::addressof(g_powerArmorPipboyScreen),
			args);
		if (result != RE::BSResource::ErrorCode::kNone || !g_powerArmorPipboyScreen) {
			REX::ERROR(
				"Could not load PADashPipboyScreen.nif (BSResource error {})",
				std::to_underlying(result));
			g_powerArmorPipboyScreen.reset();
			return false;
		}

		// These are the exact local-space values applied by
		// PowerArmorGeometry::BackgroundTaskFinishedLoading in 1.10.163.
		g_powerArmorPipboyScreen->SetLocalTranslate({ -0.5F, 325.0F, -37.0F });
		RE::NiUpdateData updateData{};
		g_powerArmorPipboyScreen->Update(updateData);
		g_powerArmorPipboyScreen->SetAppCulled(false);

		REX::INFO("Loaded PADashPipboyScreen.nif without the Power Armor dashboard");
		return true;
	}

	[[nodiscard]] bool EnsurePowerArmorPipboyGeometry()
	{
		if (!LoadPowerArmorPipboyScreen()) {
			return false;
		}

		auto* geometry = RE::PowerArmorGeometry::GetSingleton();
		if (!geometry) {
			REX::ERROR("PowerArmorGeometry is not available; keeping the wrist Pip-Boy");
			return false;
		}

		if (geometry->pipboyPAGlass) {
			return true;
		}

		geometry->pipboyPAGlass = g_powerArmorPipboyScreen;
		REX::INFO("Attached the standalone screen to PowerArmorGeometry::pipboyPAGlass");
		return true;
	}

	bool UsePowerArmorPipboy(const RE::Actor& a_actor)
	{
		return g_actorInPowerArmor(a_actor) || g_forceThisOpen.load(std::memory_order_relaxed);
	}

	bool UsePowerArmorPipboyAudio(const RE::Actor& a_actor)
	{
		return g_actorInPowerArmor(a_actor) ||
		       (g_powerArmorAudio && g_forceThisOpen.load(std::memory_order_relaxed));
	}

	bool UsePowerArmorPipboyLight(const RE::Actor& a_actor)
	{
		return g_actorInPowerArmor(a_actor) ||
		       (g_keepPipboyLightOn && g_forceThisOpen.load(std::memory_order_relaxed));
	}

	[[nodiscard]] bool IsForcedPipboyCloseEvent(const RE::InputEvent* a_event)
	{
		if (!g_forceThisOpen.load(std::memory_order_relaxed) || !a_event) {
			return false;
		}

		const auto* buttonEvent = a_event->As<RE::ButtonEvent>();
		if (!buttonEvent || !buttonEvent->QJustPressed()) {
			return false;
		}

		// Gameplay calls the toggle "Pipboy", while the BasicMenuNav context can
		// remap the same control to "Cancel" after the menu is on the stack. Match
		// both semantic events, and retain the physical Tab identity as a fallback.
		const auto& userEvent = buttonEvent->QUserEvent();
		return userEvent == "Pipboy"sv || userEvent == "Cancel"sv ||
		       buttonEvent->GetBSButtonCode() == RE::BS_BUTTON_CODE::kTab;
	}

	bool ShouldHandleForcedPipboyClose(
		RE::BSInputEventUser* a_inputUser,
		const RE::InputEvent* a_event)
	{
		// PipboyMenu normally rejects every event while any PipboyManager transition
		// flag is set. Always admit the toggle button for our immediate presentation
		// so it can reach the native close fallback below.
		return IsForcedPipboyCloseEvent(a_event) ||
		       g_pipboyMenuShouldHandleEvent(a_inputUser, a_event);
	}

	void HandleForcedPipboyClose(
		RE::BSInputEventUser* a_inputUser,
		const RE::ButtonEvent* a_event)
	{
		if (IsForcedPipboyCloseEvent(a_event)) {
			if (auto* manager = RE::PipboyManager::GetSingleton()) {
				const bool activeBefore = manager->QPipboyActive();
				REX::INFO(
					"Forced close input: event='{}' code=0x{:X} active={} opening={} closing={} loweringReason={}",
					a_event->QUserEvent().c_str(),
					static_cast<std::uint32_t>(a_event->GetBSButtonCode()),
					activeBefore,
					manager->pipboyOpening,
					manager->pipboyClosing,
					manager->loweringReason.underlying());

				if (!activeBefore) {
					SetPipboyActive(manager, true);
					REX::WARN("Repaired missing Pip-Boy active state before close");
				}

				// A normal kHide eventually calls PlayPipboyCloseAnim(false), which queues
				// a lower-wrist behavior-graph event. Our forced open never raised the
				// wrist, so that completion event cannot arrive. The native no-animation
				// branch performs OnPipboyCloseAnim/OnPipboyClosed synchronously and still
				// runs the complete engine teardown.
				manager->PlayPipboyCloseAnim(true);
				const_cast<RE::ButtonEvent*>(a_event)->handled =
					RE::InputEvent::HANDLED_RESULT::kStop;
				g_forceThisOpen.store(false, std::memory_order_relaxed);
				REX::INFO(
					"Completed immediate native close: active={} opening={} closing={}",
					manager->QPipboyActive(),
					manager->pipboyOpening,
					manager->pipboyClosing);
				return;
			}

			REX::ERROR("PipboyManager is unavailable; passing the Pip-Boy toggle to vanilla");
		}

		g_pipboyMenuOnButtonEvent(a_inputUser, a_event);
	}

	void OpenPipboyWithoutWristAnimation(
		RE::PipboyManager* a_manager,
		const RE::BSFixedString& a_menuName)
	{
		// Reset at the beginning of the next open, never during shutdown. The close
		// path tests ActorInPowerArmor several more times after its audio check; an
		// early reset sends a forced open into the wrist-lowering behavior graph,
		// whose completion event cannot arrive because the wrist was never raised.
		g_forceThisOpen.store(false, std::memory_order_relaxed);

		if (!g_forcePowerArmorPipboy) {
			g_playPipboyOpenAnim(a_manager, a_menuName);
			return;
		}

		const auto* player = RE::PlayerCharacter::GetSingleton();
		if (player && g_actorInPowerArmor(*player)) {
			// Preserve the genuine PA behavior graph and presentation unchanged.
			g_playPipboyOpenAnim(a_manager, a_menuName);
			return;
		}

		if (!EnsurePowerArmorPipboyGeometry()) {
			g_playPipboyOpenAnim(a_manager, a_menuName);
			return;
		}

		g_forceThisOpen.store(true, std::memory_order_relaxed);

		// PlayPipboyOpenAnim always queues the arm-raise idle and waits for the graph to
		// report back; there is no code branch for power armor, the PA skeleton simply
		// resolves it instantly. Going through the generic path with a_noAnim set skips
		// the wait and hands straight off to OnPipboyOpenAnim.
		static REL::Relocation<PlayPipboyGenericOpenAnim_t> playGenericOpen{
			REL::ID(809076)
		};
		const RE::BSFixedString noAnimationEvent{};
		playGenericOpen(a_manager, a_menuName, noAnimationEvent, true);

		// The immediate generic path should establish this in OnPipboyOpened. Repair
		// it through the engine's event-producing setter if that handoff was skipped;
		// PlayPipboyCloseAnim refuses to run at all while this value is false.
		if (!a_manager->QPipboyActive()) {
			SetPipboyActive(a_manager, true);
			REX::WARN("Generic open omitted Pip-Boy active state; repaired it");
		}
		REX::INFO("Forced Pip-Boy open completed: active={}", a_manager->QPipboyActive());
	}

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

	[[nodiscard]] bool InstallHooks()
	{
		// The OG ID is confirmed by the 1.10.163 Address Library and the BGS
		// byte-signature corpus. The post-NG equivalent is ID 2219437.
		const auto actorInPowerArmorAddress = REL::ID(1176757).address();

		g_actorInPowerArmor = reinterpret_cast<ActorInPowerArmor_t>(actorInPowerArmorAddress);

		std::vector<std::uintptr_t> presentationAddresses;
		presentationAddresses.reserve(kPresentationSites.size());

		if (!ResolveSites(kPresentationSites, actorInPowerArmorAddress, presentationAddresses)) {
			return false;
		}

		std::vector<std::uintptr_t> openAudioAddresses;
		if (!ResolveSites(kOpenAudioSites, actorInPowerArmorAddress, openAudioAddresses)) {
			return false;
		}

		std::vector<std::uintptr_t> closeAudioAddresses;
		if (!ResolveSites(kCloseAudioSites, actorInPowerArmorAddress, closeAudioAddresses)) {
			return false;
		}

		std::vector<std::uintptr_t> lightAddresses;
		if (!ResolveSites(kPipboyLightSites, actorInPowerArmorAddress, lightAddresses)) {
			return false;
		}

		// The Tab handler calls PlayPipboyOpenAnim directly. Redirect it through the
		// generic no-animation path used by immediate menu opens.
		const auto tabHandlerCall = REL::ID(181358).address() + 0x23F;
		const auto companionUseItemCall = REL::Offset(0x9FC974).address();
		const auto playPipboyOpenAnimAddress = REL::ID(663900).address();
		if (*reinterpret_cast<const std::uint8_t*>(tabHandlerCall) != 0xE8 ||
			GetCallTarget(tabHandlerCall) != playPipboyOpenAnimAddress) {
			REX::ERROR("Unexpected Tab-handler call at 0x{:X}; refusing to patch", tabHandlerCall);
			return false;
		}
		if (*reinterpret_cast<const std::uint8_t*>(companionUseItemCall) != 0xE8 ||
			GetCallTarget(companionUseItemCall) != playPipboyOpenAnimAddress) {
			REX::ERROR("Unexpected companion use-item call at 0x{:X}; refusing to patch", companionUseItemCall);
			return false;
		}

		// PipboyMenu's secondary BSInputEventUser vtable. Hook the two input methods
		// needed to provide a native toggle-to-close fallback for forced opens. The
		// exact entries and function IDs are validated before any hooks are written.
		REL::Relocation<std::uintptr_t> pipboyMenuInputVtable{ REL::ID(85678) };
		constexpr std::size_t shouldHandleEventIndex = 1;
		constexpr std::size_t onButtonEventIndex = 8;
		const auto* vtableEntries = reinterpret_cast<const std::uintptr_t*>(
			pipboyMenuInputVtable.address());
		if (vtableEntries[shouldHandleEventIndex] != REL::ID(607291).address() ||
			vtableEntries[onButtonEventIndex] != REL::ID(75248).address()) {
			REX::ERROR("Unexpected PipboyMenu input vtable; refusing to patch");
			return false;
		}

		auto& trampoline = REL::GetTrampoline();
		for (const auto address : presentationAddresses) {
			trampoline.write_call<5>(address, UsePowerArmorPipboy);
		}
		for (const auto address : openAudioAddresses) {
			trampoline.write_call<5>(address, UsePowerArmorPipboyAudio);
		}
		for (const auto address : closeAudioAddresses) {
			trampoline.write_call<5>(address, UsePowerArmorPipboyAudio);
		}
		for (const auto address : lightAddresses) {
			trampoline.write_call<5>(address, UsePowerArmorPipboyLight);
		}

		g_playPipboyOpenAnim = reinterpret_cast<PlayPipboyOpenAnim_t>(
			trampoline.write_call<5>(tabHandlerCall, OpenPipboyWithoutWristAnimation));
		trampoline.write_call<5>(companionUseItemCall, OpenPipboyWithoutWristAnimation);

		g_pipboyMenuShouldHandleEvent = reinterpret_cast<PipboyMenuShouldHandleEvent_t>(
			pipboyMenuInputVtable.write_vfunc(shouldHandleEventIndex, ShouldHandleForcedPipboyClose));
		g_pipboyMenuOnButtonEvent = reinterpret_cast<PipboyMenuOnButtonEvent_t>(
			pipboyMenuInputVtable.write_vfunc(onButtonEventIndex, HandleForcedPipboyClose));

		REX::INFO(
			"Installed {} presentation hooks, 2 no-animation open overrides, and the forced-close input fallback",
			presentationAddresses.size());
		return true;
	}

	void OnF4SEMessage(F4SE::MessagingInterface::Message* a_message)
	{
		if (a_message->type == F4SE::MessagingInterface::kGameDataReady && a_message->data &&
			g_forcePowerArmorPipboy) {
			// Warm the resource cache outside the input handler. If this fails, the first
			// open retries and then safely falls back to the wrist presentation.
			(void)LoadPowerArmorPipboyScreen();
		}
	}
}

F4SE_PLUGIN_LOAD(const F4SE::LoadInterface* a_f4se)
{
	F4SE::Init(a_f4se, {
		.log = true,
		.logName = "PowerArmorPipBoyUI",
		.trampoline = true,
		.trampolineSize = 256,
	});

	if (a_f4se->RuntimeVersion() != F4SE::RUNTIME_1_10_163) {
		REX::ERROR("Unsupported Fallout 4 runtime {}", a_f4se->RuntimeVersion().string());
		return false;
	}

	LoadSettings();
	if (!InstallHooks()) {
		return false;
	}

	const auto* messaging = F4SE::GetMessagingInterface();
	if (!messaging || !messaging->RegisterListener(OnF4SEMessage)) {
		// Preloading is only an optimization. The input hook performs the same load
		// lazily, so do not fail plugin load after the call sites have been patched.
		REX::WARN("Could not register the F4SE message listener; the screen will load on first open");
	}

	return true;
}

extern "C"
{
	F4SE_EXPORT bool F4SEPlugin_Query(const F4SE::QueryInterface* a_f4se, F4SE::PluginInfo* a_info)
	{
		a_info->name = Version::PROJECT.data();
		a_info->infoVersion = F4SE::PluginInfo::kVersion;
		a_info->version = Version::MAJOR;

		if (a_f4se->IsEditor()) {
			return false;
		}
		return true;
	}
}
