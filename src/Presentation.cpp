#include "Presentation.h"

#include "Diagnostics.h"
#include "Hooks.h"
#include "RainOverlay.h"
#include "Settings.h"

namespace PowerArmorPipBoyUI::Presentation
{
	namespace
	{
		using SetPipboyActive_t = bool (*)(
			RE::BSTValueEventSource<RE::IsPipboyActiveEvent>*,
			const bool*);

		RE::NiPointer<RE::NiNode> g_powerArmorPipboyScreen;
		RE::NiPointer<RE::NiNode> g_displacedPowerArmorPipboyScreen;
		std::atomic_bool g_forceThisOpen = false;
		std::atomic_bool g_loggedFirstPersonFreeze = false;
		std::atomic_bool g_deferNextScreenReveal = true;
		std::atomic_bool g_ownsPowerArmorPipboyGlass = false;
		std::atomic_bool g_menuOpenCloseSinkRegistered = false;
		std::atomic_uint64_t g_terminalReturnGeneration = 0;
		std::atomic_uintptr_t g_terminalReturnScreen = 0;
		std::atomic_uint32_t g_terminalReturnFrames = 0;

		void SuppressTerminalReturnFlash();

		class MenuOpenCloseSink final : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
		{
		public:
			RE::BSEventNotifyControl ProcessEvent(
				const RE::MenuOpenCloseEvent& a_event,
				RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
			{
				if (!a_event.opening && a_event.menuName == RE::TerminalMenu::MENU_NAME) {
					SuppressTerminalReturnFlash();
				}
				return RE::BSEventNotifyControl::kContinue;
			}
		};

		MenuOpenCloseSink g_menuOpenCloseSink;

		struct ShaderState
		{
			RE::BSShaderProperty* property;
			RE::BSShaderMaterial* material;
		};

		[[nodiscard]] std::vector<ShaderState> CollectShaderStates(RE::NiAVObject* a_root)
		{
			std::vector<ShaderState> states;
			RE::BSVisit::TraverseScenegraphGeometries(
				a_root,
				[&states](RE::BSGeometry* a_geometry) {
					for (const auto& property : a_geometry->properties) {
						auto* shader = netimmerse_cast<RE::BSShaderProperty*>(property.get());
						if (!shader) {
							continue;
						}

						bool alreadyRecorded = false;
						for (const auto& state : states) {
							if (state.property == shader) {
								alreadyRecorded = true;
								break;
							}
						}
						if (!alreadyRecorded) {
							states.push_back({ shader, shader->material });
						}
					}
					return RE::BSVisitControl::kContinue;
				});
			return states;
		}

		[[nodiscard]] bool IsSourceProperty(
			const RE::BSShaderProperty* a_property,
			const std::span<const ShaderState> a_sourceStates)
		{
			for (const auto& state : a_sourceStates) {
				if (state.property == a_property) {
					return true;
				}
			}
			return false;
		}

		[[nodiscard]] bool IsSourceMaterial(
			const RE::BSShaderMaterial* a_material,
			const std::span<const ShaderState> a_sourceStates)
		{
			for (const auto& state : a_sourceStates) {
				if (state.material && state.material == a_material) {
					return true;
				}
			}
			return false;
		}

		[[nodiscard]] bool IsolateShaderMaterials(
			RE::NiNode& a_clone,
			const std::span<const ShaderState> a_sourceStates)
		{
			auto cloneStates = CollectShaderStates(std::addressof(a_clone));
			if (cloneStates.empty() || cloneStates.size() != a_sourceStates.size()) {
				REX::ERROR(
					"PADashPipboyScreen clone has an unexpected shader-property count "
					"(source {}, clone {})",
					a_sourceStates.size(),
					cloneStates.size());
				return false;
			}

			// A cloned scene graph is not isolated if any geometry still points at a
			// shader property owned by the cached source. Check before mutating anything.
			for (const auto& state : cloneStates) {
				if (IsSourceProperty(state.property, a_sourceStates)) {
					REX::ERROR(
						"PADashPipboyScreen clone retained source shader property {:p}",
						static_cast<const void*>(state.property));
					return false;
				}
			}

			for (std::size_t index = 0; index < cloneStates.size(); ++index) {
				auto& state = cloneStates[index];
				if (!state.material) {
					REX::ERROR(
						"PADashPipboyScreen clone shader property {} has no material",
						index);
					return false;
				}

				const auto* materialBefore = state.material;
				// The true flag asks the engine to install a unique copy of the supplied
				// material instead of retaining a shared material-database instance.
				state.property->SetMaterial(state.material, true);
				state.material = state.property->material;

				DiagnosticLog(
					"Isolated PA screen shader {}: sourceProperty={:p} cloneProperty={:p} "
					"sourceMaterial={:p} materialBefore={:p} materialAfter={:p}",
					index,
					static_cast<const void*>(a_sourceStates[index].property),
					static_cast<const void*>(state.property),
					static_cast<const void*>(a_sourceStates[index].material),
					static_cast<const void*>(materialBefore),
					static_cast<const void*>(state.material));

				if (!state.material || IsSourceMaterial(state.material, a_sourceStates)) {
					REX::ERROR(
						"PADashPipboyScreen clone shader {} did not receive a unique material",
						index);
					return false;
				}
			}

			return true;
		}

		[[nodiscard]] RE::NiPointer<RE::NiNode> ClonePowerArmorPipboyScreen(
			RE::NiNode& a_source)
		{
			const auto sourceStates = CollectShaderStates(std::addressof(a_source));
			if (sourceStates.empty()) {
				REX::ERROR("PADashPipboyScreen source has no shader property");
				return nullptr;
			}

			RE::NiCloningProcess cloning{};
			// CopyType controls NiObjectNET copy/name behavior; material uniqueness is
			// established explicitly below.
			cloning.copyType = RE::NiCloningProcess::CopyType::kCopyExact;
			cloning.appendChar = '\0';
			cloning.scale = { 1.0F, 1.0F, 1.0F };

			RE::NiPointer<RE::NiObject> clonedObject{ a_source.CreateClone(cloning) };
			if (!clonedObject) {
				REX::ERROR("Could not create a PADashPipboyScreen scene-graph clone");
				return nullptr;
			}

			// Resolve cloned cross-references after every object has been entered in the
			// clone map. Bethesda's cloning flow invokes this on the source graph.
			a_source.ProcessClone(cloning);

			auto* cloneNode = clonedObject->IsNode();
			if (!cloneNode) {
				REX::ERROR("PADashPipboyScreen clone is not an NiNode");
				return nullptr;
			}

			RE::NiPointer<RE::NiNode> clone{ cloneNode };
			if (!IsolateShaderMaterials(*clone, sourceStates)) {
				return nullptr;
			}

			DiagnosticLog(
				"Cloned PADashPipboyScreen source={:p} clone={:p} shaderProperties={}",
				static_cast<const void*>(std::addressof(a_source)),
				static_cast<const void*>(clone.get()),
				sourceStates.size());
			return clone;
		}

		bool SetPipboyActive(RE::PipboyManager* a_manager, const bool a_active)
		{
			// BSTValueEventSource's engine setter locks the value and broadcasts an
			// IsPipboyActiveEvent when it changes. This is the same function called by
			// PipboyManager::OnPipboyOpened/Closed in Fallout 4 1.10.163.
			static REL::Relocation<SetPipboyActive_t> setActive{ REL::ID(318434) };
			return setActive(std::addressof(a_manager->pipboyActive), std::addressof(a_active));
		}

		// The PA presentation needs only this screen quad. PowerArmorGeometry normally loads
		// it together with the dashboard and rain geometry after a PreloadPowerArmor event.
		// Loading just the quad avoids invoking that subsystem's completion callback out of
		// sequence and avoids creating any of the PowerArmorHUDMenu geometry.
		[[nodiscard]] RE::BSModelDB::DBTraits::ArgsType MakePowerArmorPipboyScreenLoadArgs()
		{
			RE::BSModelDB::DBTraits::ArgsType args{};
			args.lodFadeMult = RE::ENUM_LOD_MULT::kNone;
			args.loadLevel = 0;
			args.prepareAfterLoad = true;
			args.faceGenModel = false;
			// A marker would make Demand report a usable node and defeat the wrist-Pip-Boy
			// fallback when the PA screen resource is missing or corrupt.
			args.useErrorMarker = false;
			args.performProcess = true;
			args.createFadeNode = true;
			args.loadTextures = true;
			return args;
		}

		[[nodiscard]] RE::BSResource::ErrorCode DemandPowerArmorPipboyScreen(
			RE::NiPointer<RE::NiNode>& a_result)
		{
			const auto args = MakePowerArmorPipboyScreenLoadArgs();
			return RE::BSModelDB::Demand(
				"Interface/Objects/PADashPipboyScreen.nif",
				std::addressof(a_result),
				args);
		}

		void PrewarmPowerArmorPipboyScreenSource()
		{
			RE::NiPointer<RE::NiNode> demandedSource;
			const auto result = DemandPowerArmorPipboyScreen(demandedSource);
			if (result != RE::BSResource::ErrorCode::kNone || !demandedSource) {
				REX::ERROR(
					"Could not prewarm PADashPipboyScreen.nif (BSResource error {})",
					std::to_underlying(result));
				return;
			}

			DiagnosticLog(
				"Prewarmed PADashPipboyScreen source {:p} without retaining a plugin clone",
				static_cast<const void*>(demandedSource.get()));
		}

		[[nodiscard]] bool LoadPowerArmorPipboyScreen()
		{
			if (g_powerArmorPipboyScreen) {
				return true;
			}

			RE::NiPointer<RE::NiNode> demandedSource;
			const auto result = DemandPowerArmorPipboyScreen(demandedSource);
			if (result != RE::BSResource::ErrorCode::kNone || !demandedSource) {
				REX::ERROR(
					"Could not load PADashPipboyScreen.nif (BSResource error {})",
					std::to_underlying(result));
				return false;
			}

			g_powerArmorPipboyScreen = ClonePowerArmorPipboyScreen(*demandedSource);
			if (!g_powerArmorPipboyScreen) {
				REX::ERROR(
					"Could not isolate PADashPipboyScreen geometry; keeping the wrist Pip-Boy");
				demandedSource.reset();
				return false;
			}
			// Do not retain the model-database source. Vanilla may demand that cached
			// object later for genuine power-armor presentation.
			demandedSource.reset();

			// These are the exact local-space values applied by
			// PowerArmorGeometry::BackgroundTaskFinishedLoading in 1.10.163.
			g_powerArmorPipboyScreen->SetLocalTranslate({ -0.5F, 325.0F, -37.0F });
			RE::NiUpdateData updateData{};
			g_powerArmorPipboyScreen->Update(updateData);
			// Keep the standalone child hidden until the Pip-Boy renderer has configured
			// its screen-attached surface. PowerArmorGeometry::ShowPipboyPAGeometry will
			// unhide it during open; the first open is reculled for one setup frame below.
			g_powerArmorPipboyScreen->SetAppCulled(true);
			// Every forced session owns a fresh clone. Give each one a setup frame before
			// reveal rather than relying on state warmed by a previous clone.
			g_deferNextScreenReveal.store(true, std::memory_order_relaxed);

			DiagnosticLog(
				"Loaded an isolated PADashPipboyScreen clone without the Power Armor dashboard");
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
				g_powerArmorPipboyScreen.reset();
				return false;
			}

			// Vanilla can retain its genuine PA screen in this slot after PA use. Keep a
			// strong reference and restore that exact value when the forced session ends.
			g_displacedPowerArmorPipboyScreen = geometry->pipboyPAGlass;
			geometry->pipboyPAGlass = g_powerArmorPipboyScreen;
			g_ownsPowerArmorPipboyGlass.store(true, std::memory_order_relaxed);
			DiagnosticLog(
				"Attached standalone screen {:p}; preserved vanilla geometry {:p}",
				static_cast<const void*>(g_powerArmorPipboyScreen.get()),
				static_cast<const void*>(g_displacedPowerArmorPipboyScreen.get()));
			return true;
		}

		void DetachStandalonePowerArmorPipboyGeometry()
		{
			// Vanilla may reuse the same cached NIF node for the genuine power-armor
			// presentation. Pointer identity therefore does not establish ownership.
			// Detach only when this plugin explicitly populated the slot.
			if (!g_ownsPowerArmorPipboyGlass.exchange(false, std::memory_order_relaxed)) {
				return;
			}

			auto* geometry = RE::PowerArmorGeometry::GetSingleton();
			if (g_powerArmorPipboyScreen && geometry &&
				geometry->pipboyPAGlass.get() == g_powerArmorPipboyScreen.get()) {
				g_powerArmorPipboyScreen->SetAppCulled(true);
				geometry->pipboyPAGlass = g_displacedPowerArmorPipboyScreen;
				DiagnosticLog(
					"Detached standalone screen and restored vanilla geometry {:p}",
					static_cast<const void*>(g_displacedPowerArmorPipboyScreen.get()));
			} else {
				DiagnosticLog("Released standalone-screen ownership after vanilla replaced the geometry");
			}
			g_displacedPowerArmorPipboyScreen.reset();
		}

		void ResetForcedPresentation(const std::string_view a_reason)
		{
			g_terminalReturnGeneration.fetch_add(1, std::memory_order_relaxed);
			g_terminalReturnFrames.store(0, std::memory_order_relaxed);
			g_terminalReturnScreen.store(0, std::memory_order_relaxed);
			const bool wasForced = g_forceThisOpen.exchange(false, std::memory_order_relaxed);
			g_loggedFirstPersonFreeze.store(false, std::memory_order_relaxed);
			DetachStandalonePowerArmorPipboyGeometry();
			if (g_powerArmorPipboyScreen) {
				DiagnosticLog(
					"Released session-owned standalone screen {:p}",
					static_cast<const void*>(g_powerArmorPipboyScreen.get()));
				g_powerArmorPipboyScreen.reset();
			}
			if (wasForced) {
				DiagnosticLog("Reset forced Pip-Boy presentation ({})", a_reason);
			}
		}

		void RevealPowerArmorPipboyScreen()
		{
			g_powerArmorPipboyScreen->SetAppCulled(false);
			RE::NiUpdateData updateData{};
			g_powerArmorPipboyScreen->Update(updateData);
		}

		void DeferFirstScreenReveal()
		{
			auto* geometry = RE::PowerArmorGeometry::GetSingleton();
			if (!g_deferNextScreenReveal.exchange(false, std::memory_order_relaxed) ||
				!g_powerArmorPipboyScreen || !geometry ||
				geometry->pipboyPAGlass.get() != g_powerArmorPipboyScreen.get()) {
				return;
			}

			// On the first open, the Interface3D renderer and Scaleform render target are
			// initialized in the same call that unhides this quad. Re-cull it before that
			// frame is presented, then reveal it on the following update once the texture
			// has valid contents. Subsequent opens use the warmed renderer immediately.
			g_powerArmorPipboyScreen->SetAppCulled(true);
			if (const auto* tasks = F4SE::GetTaskInterface()) {
				tasks->AddTask([] {
					auto* manager = RE::PipboyManager::GetSingleton();
					if (g_forceThisOpen.load(std::memory_order_relaxed) && manager &&
						manager->QPipboyActive() && g_powerArmorPipboyScreen) {
						RevealPowerArmorPipboyScreen();
						DiagnosticLog("Revealed the warmed Pip-Boy screen after its setup frame");
					}
				});
			} else {
				RevealPowerArmorPipboyScreen();
			}
		}

		[[nodiscard]] RE::NiNode* GetTerminalReturnScreen()
		{
			auto* manager = RE::PipboyManager::GetSingleton();
			auto* ui = RE::UI::GetSingleton();
			const auto* player = RE::PlayerCharacter::GetSingleton();
			if (!manager || !manager->QPipboyActive() || !ui ||
				!ui->GetMenuOpen<RE::PipboyMenu>() || !player ||
				(!g_forceThisOpen.load(std::memory_order_relaxed) &&
					!Hooks::ActorInPowerArmor(*player))) {
				return nullptr;
			}

			auto* geometry = RE::PowerArmorGeometry::GetSingleton();
			return geometry ? geometry->pipboyPAGlass.get() : nullptr;
		}

		void SetTerminalReturnScreenCulled(RE::NiNode& a_screen, const bool a_culled)
		{
			a_screen.SetAppCulled(a_culled);
			RE::NiUpdateData updateData{};
			a_screen.Update(updateData);
		}

		void SuppressTerminalReturnFlash()
		{
			auto* screen = GetTerminalReturnScreen();
			if (!screen) {
				return;
			}

			g_terminalReturnGeneration.fetch_add(1, std::memory_order_relaxed);
			g_terminalReturnScreen.store(
				reinterpret_cast<std::uintptr_t>(screen),
				std::memory_order_relaxed);
			g_terminalReturnFrames.store(2, std::memory_order_release);
			SetTerminalReturnScreenCulled(*screen, true);
			DiagnosticLog(
				"Concealed PA Pip-Boy screen for terminal render-target handoff (screen={:p})",
				static_cast<const void*>(screen));
		}

		void AdvanceTerminalReturnFlashSuppression()
		{
			const auto frames = g_terminalReturnFrames.load(std::memory_order_acquire);
			if (frames == 0) {
				return;
			}

			const auto generation =
				g_terminalReturnGeneration.load(std::memory_order_relaxed);
			const auto expectedScreen =
				g_terminalReturnScreen.load(std::memory_order_relaxed);
			auto* screen = GetTerminalReturnScreen();
			if (!screen || reinterpret_cast<std::uintptr_t>(screen) != expectedScreen) {
				g_terminalReturnFrames.store(0, std::memory_order_release);
				g_terminalReturnScreen.store(0, std::memory_order_relaxed);
				return;
			}

			if (frames > 1) {
				SetTerminalReturnScreenCulled(*screen, true);
				if (g_terminalReturnGeneration.load(std::memory_order_relaxed) == generation) {
					g_terminalReturnFrames.store(frames - 1, std::memory_order_release);
					DiagnosticLog(
						"Held PA Pip-Boy screen concealed across a PipboyMenu frame (screen={:p})",
						static_cast<const void*>(screen));
				}
				return;
			}

			auto* ui = RE::UI::GetSingleton();
			if (!ui || ui->GetMenuOpen<RE::TerminalMenu>()) {
				SetTerminalReturnScreenCulled(*screen, true);
				return;
			}

			SetTerminalReturnScreenCulled(*screen, false);
			if (g_terminalReturnGeneration.load(std::memory_order_relaxed) == generation) {
				g_terminalReturnFrames.store(0, std::memory_order_release);
				g_terminalReturnScreen.store(0, std::memory_order_relaxed);
				DiagnosticLog(
					"Revealed PA Pip-Boy screen after terminal render-target handoff (screen={:p})",
					static_cast<const void*>(screen));
			}
		}

		void RegisterMenuOpenCloseSink()
		{
			if (g_menuOpenCloseSinkRegistered.load(std::memory_order_relaxed)) {
				return;
			}

			if (auto* ui = RE::UI::GetSingleton()) {
				ui->RegisterSink(std::addressof(g_menuOpenCloseSink));
				g_menuOpenCloseSinkRegistered.store(true, std::memory_order_relaxed);
				DiagnosticLog("Registered PipboyMenu-frame terminal-return flash suppression");
			}
		}

		[[nodiscard]] bool IsForcedPipboyToggleEvent(const RE::InputEvent* a_event)
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

		[[nodiscard]] bool IsNestedPipboyPresentationOpen()
		{
			const auto* ui = RE::UI::GetSingleton();
			if (!ui) {
				return false;
			}

			if (ui->GetMenuOpen<RE::PipboyHolotapeMenu>()) {
				return true;
			}
			if (ui->GetMenuOpen<RE::TerminalMenu>()) {
				return true;
			}

			const auto pipboyMenu = ui->GetMenu<RE::PipboyMenu>();
			return pipboyMenu &&
			       (pipboyMenu->showingModalMessage || pipboyMenu->pipboyHiddenByAnotherMenu);
		}

		[[nodiscard]] bool IsForcedPipboyCloseEvent(const RE::InputEvent* a_event)
		{
			if (!IsForcedPipboyToggleEvent(a_event) || IsNestedPipboyPresentationOpen()) {
				return false;
			}

			// PipboyMenu owns Tab/Cancel while a nested presentation is active. Item
			// inspection clears pipboyExamineMode before it finishes raising the Pip-Boy,
			// but loweringReason remains kInspect until the inventory has been restored.
			// Closing during either phase bypasses that unwind and leaves its input layer
			// enabled, locking gameplay controls.
			const auto* manager = RE::PipboyManager::GetSingleton();
			return !manager ||
			       (!manager->pipboyExamineMode && !manager->pipboyRaising &&
				   manager->loweringReason.underlying() ==
					   std::to_underlying(RE::PipboyManager::LOWER_REASON::kNone));
		}

		void CompleteForcedPipboyClose(RE::PipboyManager* a_manager)
		{
			// Pick up an audio-setting edit made while this menu was open. Presentation
			// mode remains latched until teardown so changing the master switch mid-menu
			// cannot strand the session in a half-forced state.
			Settings::Load();

			if (!a_manager->QPipboyActive()) {
				SetPipboyActive(a_manager, true);
				REX::WARN("Repaired missing Pip-Boy active state before close");
			}

			const bool hadPendingItemAnimation = a_manager->itemAnimOnClose != nullptr;
			const bool hadPendingFastTravel = static_cast<bool>(a_manager->fastTravelLocation);

			// Start the normal PA close. This selects the PA completion event and keeps
			// itemAnimOnClose/fastTravelLocation intact. The native a_noAnim=true path
			// cannot be used: OnPipboyCloseAnim deliberately clears both deferred fields
			// when it sees that event. Since the player's non-PA behavior graph cannot
			// report the PA completion event, deliver it synchronously ourselves.
			a_manager->PlayPipboyCloseAnim(false);
			if (a_manager->QPipboyActive()) {
				a_manager->OnPipboyCloseAnim();
			}

			// OnPipboyCloseAnim normally reaches the authoritative closedown hook. Keep
			// this idempotent reset as a final guard if the engine returned early.
			ResetForcedPresentation("synthetic close completion"sv);
			DiagnosticLog(
				"Completed synthetic PA close: active={} opening={} closing={} pendingItem={} pendingFastTravel={}",
				a_manager->QPipboyActive(),
				a_manager->pipboyOpening,
				a_manager->pipboyClosing,
				hadPendingItemAnimation,
				hadPendingFastTravel);
		}
	}

	bool UsePowerArmorPipboy(const RE::Actor& a_actor)
	{
		return Hooks::ActorInPowerArmor(a_actor) ||
		       g_forceThisOpen.load(std::memory_order_relaxed);
	}

	bool UsePowerArmorPipboyAudio(const RE::Actor& a_actor)
	{
		return Hooks::ActorInPowerArmor(a_actor) ||
		       (Settings::PowerArmorAudio() &&
			   g_forceThisOpen.load(std::memory_order_relaxed));
	}

	bool UsePowerArmorPipboyLight(const RE::Actor& a_actor)
	{
		return Hooks::ActorInPowerArmor(a_actor) ||
		       (Settings::KeepPipboyLightOn() &&
			   g_forceThisOpen.load(std::memory_order_relaxed));
	}

	bool ShouldHandleForcedPipboyClose(
		RE::BSInputEventUser* a_inputUser,
		const RE::InputEvent* a_event)
	{
		// PipboyMenu normally rejects every event while any PipboyManager transition
		// flag is set. Always admit the toggle button for our immediate presentation
		// so it can reach the native close fallback below.
		return (IsForcedPipboyToggleEvent(a_event) && !IsNestedPipboyPresentationOpen()) ||
		       Hooks::PipboyMenuShouldHandleEvent(a_inputUser, a_event);
	}

	void PlayPipboyCloseForForcedPresentation(RE::PipboyManager* a_manager, const bool a_noAnim)
	{
		if (g_forceThisOpen.load(std::memory_order_relaxed) && !a_noAnim) {
			CompleteForcedPipboyClose(a_manager);
			return;
		}

		a_manager->PlayPipboyCloseAnim(a_noAnim);
	}

	void PlayPipboyLoadHolotapeForForcedPresentation(
		RE::PipboyManager* a_manager,
		RE::BGSNote* a_holotape,
		const bool a_noAnim)
	{
		const bool forceNoAnimation =
			g_forceThisOpen.load(std::memory_order_relaxed) && !a_noAnim;
		if (forceNoAnimation) {
			DiagnosticLog("Using the native no-animation path for a forced holotape load");
		}

		a_manager->PlayPipboyLoadHolotapeAnim(a_holotape, a_noAnim || forceNoAnimation);
	}

	void ClosedownPipboyAndReset(RE::PipboyManager* a_manager)
	{
		// Keep the forced state latched through ClosedownPipboy itself: its audio and
		// presentation checks still need to see the PA branch. Reset only after vanilla
		// has finished restoring menu, input, light, and deferred-action state.
		Hooks::ClosedownPipboy(a_manager);
		ResetForcedPresentation("engine closedown"sv);
	}

	void HandleForcedPipboyClose(
		RE::BSInputEventUser* a_inputUser,
		const RE::ButtonEvent* a_event)
	{
		if (IsForcedPipboyToggleEvent(a_event) && !IsForcedPipboyCloseEvent(a_event)) {
			if (const auto* manager = RE::PipboyManager::GetSingleton()) {
				DiagnosticLog(
					"Passing Pip-Boy toggle to nested presentation: examine={} raising={} loweringReason={}",
					manager->pipboyExamineMode,
					manager->pipboyRaising,
					manager->loweringReason.underlying());
			}
			Hooks::PipboyMenuOnButtonEvent(a_inputUser, a_event);
			return;
		}

		if (IsForcedPipboyCloseEvent(a_event)) {
			if (auto* manager = RE::PipboyManager::GetSingleton()) {
				const bool activeBefore = manager->QPipboyActive();
				DiagnosticLog(
					"Forced close input: event='{}' code=0x{:X} active={} opening={} closing={} loweringReason={}",
					a_event->QUserEvent().c_str(),
					static_cast<std::uint32_t>(a_event->GetBSButtonCode()),
					activeBefore,
					manager->pipboyOpening,
					manager->pipboyClosing,
					manager->loweringReason.underlying());

				CompleteForcedPipboyClose(manager);
				// PipboyMenu receives a const event even though Bethesda's input dispatch
				// convention marks handled state in the engine-owned event object itself.
				const_cast<RE::ButtonEvent*>(a_event)->handled =
					RE::InputEvent::HANDLED_RESULT::kStop;
				return;
			}

			REX::ERROR("PipboyManager is unavailable; passing the Pip-Boy toggle to vanilla");
		}

		Hooks::PipboyMenuOnButtonEvent(a_inputUser, a_event);
	}

	void UpdateFirstPersonCameraForForcedPresentation(
		RE::TESCameraState* a_state,
		RE::BSTSmartPointer<RE::TESCameraState>& a_nextState)
	{
		if (g_forceThisOpen.load(std::memory_order_relaxed)) {
			// The first-person state samples the animated camera node and applies its
			// locomotion smoothing every frame, even while the Pip-Boy menu has paused
			// the world. A real PA graph supplies a stationary camera pose here. Freeze
			// only this state for our screen-only presentation instead of entering a
			// Pip-Boy camera mode, which changes the camera stack and return view.
			if (!g_loggedFirstPersonFreeze.exchange(true, std::memory_order_relaxed)) {
				DiagnosticLog("Froze first-person camera updates for the forced Pip-Boy session");
			}
			return;
		}

		Hooks::FirstPersonStateUpdate(a_state, a_nextState);
	}

	void AdvancePipboyMenuForTerminalReturn(
		RE::IMenu* a_menu,
		const float a_timeDelta,
		const std::uint64_t a_time)
	{
		Hooks::PipboyMenuAdvanceMovie(a_menu, a_timeDelta, a_time);
		AdvanceTerminalReturnFlashSuppression();
	}

	void OpenPipboyWithoutWristAnimation(
		RE::PipboyManager* a_manager,
		const RE::BSFixedString& a_menuName)
	{
		// INI changes become effective at the next stable presentation boundary;
		// Fallout 4 does not need to be restarted.
		Settings::Load();

		// Repair any presentation state left behind by a nonstandard menu teardown
		// before deciding how this new session should open.
		ResetForcedPresentation("new open boundary"sv);

		if (!Settings::ForcePowerArmorPipboy()) {
			a_manager->PlayPipboyOpenAnim(a_menuName);
			return;
		}

		const auto* player = RE::PlayerCharacter::GetSingleton();
		const bool playerInPowerArmor = player && Hooks::ActorInPowerArmor(*player);
		DiagnosticLog("Pip-Boy open classified as genuine power armor: {}", playerInPowerArmor);
		if (playerInPowerArmor) {
			const auto* geometry = RE::PowerArmorGeometry::GetSingleton();
			DiagnosticLog(
				"Genuine PA geometry identity: pipboyPAGlass={:p} pluginClone={:p} ownsSlot={}",
				static_cast<const void*>(geometry ? geometry->pipboyPAGlass.get() : nullptr),
				static_cast<const void*>(g_powerArmorPipboyScreen.get()),
				g_ownsPowerArmorPipboyGlass.load(std::memory_order_relaxed));
			// Preserve the genuine PA behavior graph and presentation unchanged.
			a_manager->PlayPipboyOpenAnim(a_menuName);
			return;
		}

		if (!EnsurePowerArmorPipboyGeometry()) {
			a_manager->PlayPipboyOpenAnim(a_menuName);
			return;
		}

		g_forceThisOpen.store(true, std::memory_order_relaxed);

		// PlayPipboyOpenAnim always queues the arm-raise idle and waits for the graph to
		// report back; there is no code branch for power armor, the PA skeleton simply
		// resolves it instantly. Going through the generic path with a_noAnim set skips
		// the wait and hands straight off to OnPipboyOpenAnim.
		const RE::BSFixedString noAnimationEvent{};
		a_manager->PlayPipboyGenericOpenAnim(a_menuName, noAnimationEvent, true);
		DeferFirstScreenReveal();

		// The immediate generic path should establish this in OnPipboyOpened. Repair
		// it through the engine's event-producing setter if that handoff was skipped;
		// PlayPipboyCloseAnim refuses to run at all while this value is false.
		if (!a_manager->QPipboyActive()) {
			SetPipboyActive(a_manager, true);
			REX::WARN("Generic open omitted Pip-Boy active state; repaired it");
		}
		DiagnosticLog("Forced Pip-Boy open completed: active={}", a_manager->QPipboyActive());
	}

	void OnF4SEMessage(F4SE::MessagingInterface::Message* a_message)
	{
		RainOverlay::OnF4SEMessage(a_message);

		if (a_message->type == F4SE::MessagingInterface::kPreLoadGame) {
			ResetForcedPresentation("pre-load game"sv);
			return;
		}

		if (a_message->type == F4SE::MessagingInterface::kGameDataReady) {
			if (!a_message->data) {
				ResetForcedPresentation("game data unloaded"sv);
				DiagnosticLog("Released forced presentation state before game-data teardown");
				return;
			}

			RegisterMenuOpenCloseSink();

			if (!Settings::ForcePowerArmorPipboy()) {
				return;
			}

			// Demand only the cached source. Retaining even an unattached clone changes the
			// genuine-PA transition fallback from gray to black, so clones are session-owned.
			PrewarmPowerArmorPipboyScreenSource();
			return;
		}

		if (a_message->type == F4SE::MessagingInterface::kPostLoadGame ||
			a_message->type == F4SE::MessagingInterface::kNewGame) {
			RegisterMenuOpenCloseSink();
			ResetForcedPresentation(
				a_message->type == F4SE::MessagingInterface::kPostLoadGame ?
					"post-load game"sv :
					"new game"sv);
			g_deferNextScreenReveal.store(true, std::memory_order_relaxed);
			if (!Settings::ForcePowerArmorPipboy()) {
				return;
			}
			// Keep only the source/model cache warm. A fresh isolated clone is created on
			// the next forced open and released as soon as that session closes.
			PrewarmPowerArmorPipboyScreenSource();
		}
	}
}
