#include "Presentation/ScreenGeometry.h"

#include "Diagnostics.h"

namespace PowerArmorPipBoyUI::Presentation::ScreenGeometry
{
	namespace
	{
		RE::NiPointer<RE::NiNode> g_screen;
		RE::NiPointer<RE::NiNode> g_displacedScreen;
		std::atomic_uint32_t g_firstRevealFrames = 0;
		std::atomic_bool g_ownsPowerArmorGlass = false;

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

		[[nodiscard]] RE::NiPointer<RE::NiNode> CloneScreen(RE::NiNode& a_source)
		{
			const auto sourceStates = CollectShaderStates(std::addressof(a_source));
			if (sourceStates.empty()) {
				REX::ERROR("PADashPipboyScreen source has no shader property");
				return nullptr;
			}

			RE::NiCloningProcess cloning{};
			cloning.copyType = RE::NiCloningProcess::CopyType::kCopyExact;
			cloning.appendChar = '\0';
			cloning.scale = { 1.0F, 1.0F, 1.0F };

			RE::NiPointer<RE::NiObject> clonedObject{ a_source.CreateClone(cloning) };
			if (!clonedObject) {
				REX::ERROR("Could not create a PADashPipboyScreen scene-graph clone");
				return nullptr;
			}

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

		[[nodiscard]] RE::BSModelDB::DBTraits::ArgsType MakeLoadArgs()
		{
			RE::BSModelDB::DBTraits::ArgsType args{};
			args.lodFadeMult = RE::ENUM_LOD_MULT::kNone;
			args.loadLevel = 0;
			args.prepareAfterLoad = true;
			args.faceGenModel = false;
			args.useErrorMarker = false;
			args.performProcess = true;
			args.createFadeNode = true;
			args.loadTextures = true;
			return args;
		}

		[[nodiscard]] RE::BSResource::ErrorCode DemandScreen(RE::NiPointer<RE::NiNode>& a_result)
		{
			const auto args = MakeLoadArgs();
			return RE::BSModelDB::Demand(
				"Interface/Objects/PADashPipboyScreen.nif",
				std::addressof(a_result),
				args);
		}

		[[nodiscard]] bool LoadScreen()
		{
			if (g_screen) {
				return true;
			}

			RE::NiPointer<RE::NiNode> demandedSource;
			const auto result = DemandScreen(demandedSource);
			if (result != RE::BSResource::ErrorCode::kNone || !demandedSource) {
				REX::ERROR(
					"Could not load PADashPipboyScreen.nif (BSResource error {})",
					std::to_underlying(result));
				return false;
			}

			g_screen = CloneScreen(*demandedSource);
			if (!g_screen) {
				REX::ERROR("Could not isolate PADashPipboyScreen geometry; keeping the wrist Pip-Boy");
				return false;
			}

			demandedSource.reset();
			g_screen->SetLocalTranslate({ -0.5F, 325.0F, -37.0F });
			RE::NiUpdateData updateData{};
			g_screen->Update(updateData);
			g_screen->SetAppCulled(true);
			DiagnosticLog("Loaded an isolated PADashPipboyScreen clone without the Power Armor dashboard");
			return true;
		}

		void Detach()
		{
			if (!g_ownsPowerArmorGlass.exchange(false, std::memory_order_relaxed)) {
				return;
			}

			auto* geometry = RE::PowerArmorGeometry::GetSingleton();
			if (g_screen && geometry && geometry->pipboyPAGlass.get() == g_screen.get()) {
				g_screen->SetAppCulled(true);
				geometry->pipboyPAGlass = g_displacedScreen;
				DiagnosticLog(
					"Detached standalone screen and restored vanilla geometry {:p}",
					static_cast<const void*>(g_displacedScreen.get()));
			} else {
				DiagnosticLog("Released standalone-screen ownership after vanilla replaced the geometry");
			}
			g_displacedScreen.reset();
		}

		void Reveal()
		{
			g_screen->SetAppCulled(false);
			RE::NiUpdateData updateData{};
			g_screen->Update(updateData);
		}
	}

	void Prewarm()
	{
		RE::NiPointer<RE::NiNode> demandedSource;
		const auto result = DemandScreen(demandedSource);
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

	bool Attach()
	{
		if (!LoadScreen()) {
			return false;
		}

		auto* geometry = RE::PowerArmorGeometry::GetSingleton();
		if (!geometry) {
			REX::ERROR("PowerArmorGeometry is not available; keeping the wrist Pip-Boy");
			g_screen.reset();
			return false;
		}

		g_displacedScreen = geometry->pipboyPAGlass;
		geometry->pipboyPAGlass = g_screen;
		g_ownsPowerArmorGlass.store(true, std::memory_order_relaxed);
		DiagnosticLog(
			"Attached standalone screen {:p}; preserved vanilla geometry {:p}",
			static_cast<const void*>(g_screen.get()),
			static_cast<const void*>(g_displacedScreen.get()));
		return true;
	}

	void Reset()
	{
		g_firstRevealFrames.store(0, std::memory_order_relaxed);
		Detach();
		if (g_screen) {
			DiagnosticLog(
				"Released session-owned standalone screen {:p}",
				static_cast<const void*>(g_screen.get()));
			g_screen.reset();
		}
	}

	void DeferFirstReveal()
	{
		auto* geometry = RE::PowerArmorGeometry::GetSingleton();
		if (!g_screen || !geometry || geometry->pipboyPAGlass.get() != g_screen.get()) {
			return;
		}

		g_screen->SetAppCulled(true);
		RE::NiUpdateData updateData{};
		g_screen->Update(updateData);
		g_firstRevealFrames.store(2, std::memory_order_release);
	}

	void AdvanceFirstReveal(const bool a_forcedSession)
	{
		const auto frames = g_firstRevealFrames.load(std::memory_order_acquire);
		if (frames == 0) {
			return;
		}

		auto* manager = RE::PipboyManager::GetSingleton();
		auto* geometry = RE::PowerArmorGeometry::GetSingleton();
		if (!a_forcedSession || !g_screen || !geometry ||
			geometry->pipboyPAGlass.get() != g_screen.get()) {
			g_firstRevealFrames.store(0, std::memory_order_release);
			return;
		}
		if (!manager || !manager->QPipboyActive()) {
			g_screen->SetAppCulled(true);
			return;
		}

		if (frames > 1) {
			g_screen->SetAppCulled(true);
			RE::NiUpdateData updateData{};
			g_screen->Update(updateData);
			g_firstRevealFrames.store(frames - 1, std::memory_order_release);
			DiagnosticLog("Held fresh Pip-Boy screen clone concealed across a PipboyMenu frame");
			return;
		}

		Reveal();
		g_firstRevealFrames.store(0, std::memory_order_release);
		DiagnosticLog("Revealed fresh Pip-Boy screen clone after its setup frame");
	}

	RE::NiNode* GetPluginScreen() noexcept
	{
		return g_screen.get();
	}

	bool OwnsPowerArmorGlass() noexcept
	{
		return g_ownsPowerArmorGlass.load(std::memory_order_relaxed);
	}
}
