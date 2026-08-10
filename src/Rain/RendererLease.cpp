#include "Rain/RendererLease.h"

#include "Diagnostics.h"
#include "Hooks.h"

namespace PowerArmorPipBoyUI::Rain::RendererLease
{
	namespace
	{
		constexpr auto kRendererName = "HUDRainRenderer"sv;
		constexpr auto kRainModelPath = "Effects/CameraAttachedFX/CameraAttach_WetArmorView.nif"sv;
		constexpr auto kHUDGlassMaterial = "Materials\\Interface\\HUDGlassFlat.BGEM"sv;
		constexpr float kRainScale = 0.57F;
		constexpr RE::NiPoint3 kRainTranslation{ 0.0F, 351.0F, 0.0F };

		struct RendererState
		{
			float opacityAlpha;
			bool enabled;
			bool clearRenderTarget;
			bool clearDepthStencilMainScreen;
			bool clearDepthStencilOffscreen;
			bool postAA;
			REX::TEnumSet<RE::Interface3D::BackgroundMode, std::int32_t> backgroundMode;
			REX::TEnumSet<RE::Interface3D::OffscreenMenuSize, std::int32_t> menuSize;
			REX::TEnumSet<RE::Interface3D::ScreenMode, std::int32_t> screenMode;
			RE::BSFixedString screenGeometryName;
			RE::BSFixedString screenMaterialName;
		};

		RE::NiPointer<RE::NiNode> g_geometry;
		RE::NiPointer<RE::NiAVObject> g_displacedRoot;
		RE::NiPointer<RE::ImageSpaceModifierInstanceForm> g_imageSpaceModifier;
		RE::Interface3D::Renderer* g_renderer = nullptr;
		std::optional<RendererState> g_displacedState;
		bool g_ownsRenderer = false;
		bool g_loggedConflict = false;
		bool g_loggedMissingModifier = false;

		[[nodiscard]] RE::BSModelDB::DBTraits::ArgsType MakeLoadArgs()
		{
			RE::BSModelDB::DBTraits::ArgsType args{};
			args.lodFadeMult = RE::ENUM_LOD_MULT::kNone;
			args.loadLevel = 0;
			args.prepareAfterLoad = true;
			args.faceGenModel = false;
			args.useErrorMarker = true;
			args.performProcess = true;
			args.createFadeNode = false;
			args.loadTextures = true;
			return args;
		}

		[[nodiscard]] bool EnsureGeometry()
		{
			if (g_geometry) {
				return true;
			}

			RE::NiPointer<RE::NiNode> geometry;
			const auto args = MakeLoadArgs();
			const auto result = RE::BSModelDB::Demand(
				kRainModelPath.data(),
				std::addressof(geometry),
				args);
			if (result != RE::BSResource::ErrorCode::kNone || !geometry) {
				REX::ERROR(
					"Could not load {} (BSResource error {})",
					kRainModelPath,
					std::to_underlying(result));
				return false;
			}

			g_geometry = std::move(geometry);
			g_geometry->SetLocalScale(kRainScale);
			g_geometry->SetLocalTranslate(kRainTranslation);
			RE::NiUpdateData updateData{};
			g_geometry->Update(updateData);
			g_geometry->SetAppCulled(true);
			DiagnosticLog(
				"Loaded independent WetArmorView instance={:p}",
				static_cast<void*>(g_geometry.get()));
			return true;
		}

		[[nodiscard]] RendererState CaptureState(const RE::Interface3D::Renderer& a_renderer)
		{
			return {
				a_renderer.opacityAlpha,
				a_renderer.enabled,
				a_renderer.clearRenderTarget,
				a_renderer.clearDepthStencilMainScreen,
				a_renderer.clearDepthStencilOffscreen,
				a_renderer.postAA,
				a_renderer.bgmode,
				a_renderer.omsize,
				a_renderer.screenmode,
				a_renderer.screenGeomName,
				a_renderer.screenMaterialName,
			};
		}

		void Configure(RE::Interface3D::Renderer& a_renderer)
		{
			a_renderer.MainScreen_SetScreenAttached3D(g_geometry.get());
			a_renderer.Offscreen_SetDisplayMode(
				RE::Interface3D::ScreenMode::kScreenAttached,
				nullptr,
				kHUDGlassMaterial.data());
			a_renderer.clearRenderTarget = false;
			a_renderer.Offscreen_SetRenderTargetSize(
				RE::Interface3D::OffscreenMenuSize::kFullFrame);
			a_renderer.postAA = true;
			a_renderer.MainScreen_SetBackgroundMode(RE::Interface3D::BackgroundMode::kLive);
			a_renderer.clearDepthStencilMainScreen = false;
			a_renderer.clearDepthStencilOffscreen = false;
			a_renderer.MainScreen_SetOpacityAlpha(1.0F);
		}

		void RestoreState(RE::Interface3D::Renderer& a_renderer, const RendererState& a_state)
		{
			a_renderer.opacityAlpha = a_state.opacityAlpha;
			a_renderer.clearRenderTarget = a_state.clearRenderTarget;
			a_renderer.clearDepthStencilMainScreen = a_state.clearDepthStencilMainScreen;
			a_renderer.clearDepthStencilOffscreen = a_state.clearDepthStencilOffscreen;
			a_renderer.postAA = a_state.postAA;
			a_renderer.bgmode = a_state.backgroundMode;
			a_renderer.omsize = a_state.menuSize;
			a_renderer.screenmode = a_state.screenMode;
			a_renderer.screenGeomName = a_state.screenGeometryName;
			a_renderer.screenMaterialName = a_state.screenMaterialName;
			if (a_state.enabled) {
				a_renderer.Enable(false);
			}
		}

		void RestoreCapturedRenderer()
		{
			if (!g_renderer) {
				return;
			}

			g_renderer->Disable();
			g_renderer->MainScreen_SetScreenAttached3D(g_displacedRoot.get());
			if (g_displacedState) {
				RestoreState(*g_renderer, *g_displacedState);
			}
		}

		void StopImageSpaceModifier()
		{
			if (g_imageSpaceModifier) {
				static_cast<RE::ImageSpaceModifierInstance*>(g_imageSpaceModifier.get())->Stop();
				g_imageSpaceModifier.reset();
			}
		}

		[[nodiscard]] bool Acquire()
		{
			if (g_ownsRenderer) {
				if (g_renderer && g_geometry &&
					g_renderer->screenAttachedElementRoot.get() == g_geometry.get()) {
					return true;
				}
				Relinquish("renderer ownership lost"sv);
				return false;
			}
			if (!EnsureGeometry()) {
				return false;
			}

			const RE::BSFixedString rendererName{ kRendererName };
			g_renderer = RE::Interface3D::Renderer::GetByName(rendererName);
			if (!g_renderer) {
				g_renderer = RE::Interface3D::Renderer::Create(
					rendererName,
					RE::UI_DEPTH_PRIORITY::kStandard,
					0.0F,
					false);
				if (!g_renderer) {
					REX::ERROR("Could not instantiate HUDRainRenderer");
					return false;
				}
				DiagnosticLog("Instantiated the vanilla-named HUDRainRenderer");
			}

			auto* const currentRoot = g_renderer->screenAttachedElementRoot.get();
			const auto* powerArmorGeometry = RE::PowerArmorGeometry::GetSingleton();
			auto* const vanillaRoot = powerArmorGeometry ? powerArmorGeometry->dbHUDRain.get() : nullptr;
			if (currentRoot && currentRoot != vanillaRoot && currentRoot != g_geometry.get()) {
				if (!g_loggedConflict) {
					REX::WARN(
						"HUDRainRenderer has an unexpected root {:p}; rain outside Power Armor is disabled until it becomes available",
						static_cast<void*>(currentRoot));
					g_loggedConflict = true;
				}
				return false;
			}

			g_displacedRoot = currentRoot;
			g_displacedState = CaptureState(*g_renderer);
			Configure(*g_renderer);
			if (g_renderer->screenAttachedElementRoot.get() != g_geometry.get()) {
				REX::ERROR("HUDRainRenderer rejected the plugin rain geometry");
				RestoreCapturedRenderer();
				g_displacedRoot.reset();
				g_displacedState.reset();
				return false;
			}

			g_ownsRenderer = true;
			g_loggedConflict = false;
			DiagnosticLog(
				"Leased HUDRainRenderer: pluginRoot={:p} displacedRoot={:p}",
				static_cast<void*>(g_geometry.get()),
				static_cast<void*>(g_displacedRoot.get()));
			return true;
		}
	}

	bool OwnsRenderer() noexcept
	{
		return g_ownsRenderer;
	}

	void Start(const std::uint64_t a_time)
	{
		if (!Acquire()) {
			return;
		}

		RE::NiUpdateData updateData{};
		updateData.time = static_cast<float>(a_time) * 0.001F;
		updateData.flags = 1;
		g_renderer->Enable(false);
		g_geometry->SetAppCulled(false);
		g_geometry->Update(updateData);

		if (auto* modifier = Hooks::GetPowerArmorHUDRainModifier()) {
			if (auto* instance = RE::ImageSpaceModifierInstanceForm::Trigger(
					modifier,
					1.0F,
					nullptr)) {
				const bool firstOwnedInstance = !g_imageSpaceModifier;
				g_imageSpaceModifier = instance;
				g_loggedMissingModifier = false;
				if (firstOwnedInstance) {
					DiagnosticLog("Enabled rain outside Power Armor");
				}
			}
		} else if (!g_loggedMissingModifier) {
			REX::WARN("Could not resolve the Power Armor HUD rain image-space modifier");
			g_loggedMissingModifier = true;
		}

	}

	void Relinquish(const std::string_view a_reason)
	{
		StopImageSpaceModifier();
		if (!g_ownsRenderer) {
			return;
		}

		if (g_geometry) {
			g_geometry->SetAppCulled(true);
		}

		if (g_renderer && g_geometry &&
			g_renderer->screenAttachedElementRoot.get() == g_geometry.get()) {
			RestoreCapturedRenderer();
			DiagnosticLog("Restored HUDRainRenderer root ({})", a_reason);
		} else {
			REX::WARN(
				"HUDRainRenderer root changed while leased; released ownership without overwriting it ({})",
				a_reason);
		}

		g_ownsRenderer = false;
		g_displacedRoot.reset();
		g_displacedState.reset();
	}

	void Reset(const std::string_view a_reason, const bool a_releaseGeometry)
	{
		Relinquish(a_reason);
		if (a_releaseGeometry) {
			g_geometry.reset();
			g_renderer = nullptr;
			g_loggedConflict = false;
			g_loggedMissingModifier = false;
		}
	}
}
