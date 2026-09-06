#include "Rain/RendererLease.h"

#include "Diagnostics.h"
#include "Hooks.h"

#include <mutex>

namespace PowerArmorPipBoyUI::Rain::RendererLease
{
	namespace
	{
		// The vanilla Power Armor lifecycle creates and releases HUDRainRenderer.
		// A unique name keeps this renderer out of that ownership domain.
		constexpr auto kRendererName = "PowerArmorPipBoyUIRainRenderer"sv;
		constexpr auto kRainModelPath = "Effects/CameraAttachedFX/CameraAttach_WetArmorView.nif"sv;
		constexpr auto kHUDGlassMaterial = "Materials\\Interface\\HUDGlassFlat.BGEM"sv;
		constexpr float kRainScale = 0.57F;
		constexpr RE::NiPoint3 kRainTranslation{ 0.0F, 351.0F, 0.0F };

		RE::NiPointer<RE::NiNode> g_geometry;
		RE::NiPointer<RE::ImageSpaceModifierInstanceForm> g_imageSpaceModifier;
		// Interface3D owns Renderer lifetime. This value is an identity token only;
		// it must match a fresh GetByName result before any dereference.
		RE::Interface3D::Renderer* g_ownedRendererIdentity = nullptr;
		std::mutex g_stateLock;
		std::atomic_bool g_ownsRenderer = false;
		bool g_loggedConflict = false;
		bool g_loggedMissingModifier = false;

		[[nodiscard]] RE::Interface3D::Renderer* FindRenderer()
		{
			const RE::BSFixedString rendererName{ kRendererName };
			return RE::Interface3D::Renderer::GetByName(rendererName);
		}

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

		void Configure(RE::Interface3D::Renderer& a_renderer)
		{
			a_renderer.Disable();
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

		void StopImageSpaceModifier()
		{
			if (g_imageSpaceModifier) {
				static_cast<RE::ImageSpaceModifierInstance*>(g_imageSpaceModifier.get())->Stop();
				g_imageSpaceModifier.reset();
			}
		}

		void ClearOwnershipState()
		{
			g_ownsRenderer.store(false, std::memory_order_release);
			g_ownedRendererIdentity = nullptr;
		}

		void AbandonOwnership(const std::string_view a_reason)
		{
			StopImageSpaceModifier();
			if (g_geometry) {
				g_geometry->SetAppCulled(true);
			}

			REX::WARN(
				"Abandoned plugin rain renderer without touching an unverified engine object: ownedRenderer={:p} ({})",
				static_cast<void*>(g_ownedRendererIdentity),
				a_reason);
			ClearOwnershipState();
		}

		[[nodiscard]] RE::Interface3D::Renderer* Acquire()
		{
			if (g_ownsRenderer.load(std::memory_order_acquire)) {
				auto* const renderer = FindRenderer();
				if (!renderer || renderer != g_ownedRendererIdentity) {
					AbandonOwnership("renderer identity changed or was released"sv);
					return nullptr;
				}
				if (!g_geometry ||
					renderer->screenAttachedElementRoot.get() != g_geometry.get()) {
					AbandonOwnership("renderer root changed"sv);
					return nullptr;
				}
				return renderer;
			}
			if (!EnsureGeometry()) {
				return nullptr;
			}

			auto* renderer = FindRenderer();
			bool createdNow = false;
			if (!renderer) {
				const RE::BSFixedString rendererName{ kRendererName };
				renderer = RE::Interface3D::Renderer::Create(
					rendererName,
					RE::UI_DEPTH_PRIORITY::kStandard,
					0.0F,
					false);
				if (!renderer) {
					REX::ERROR("Could not instantiate the plugin rain renderer");
					return nullptr;
				}
				createdNow = true;
			}

			auto* const currentRoot = renderer->screenAttachedElementRoot.get();
			if (currentRoot && currentRoot != g_geometry.get()) {
				if (!g_loggedConflict) {
					REX::WARN(
						"Plugin rain renderer has an unexpected root {:p}; synthetic rain is disabled until it becomes available",
						static_cast<void*>(currentRoot));
					g_loggedConflict = true;
				}
				return nullptr;
			}

			Configure(*renderer);
			if (renderer->screenAttachedElementRoot.get() != g_geometry.get()) {
				REX::ERROR("Plugin rain renderer rejected the plugin rain geometry");
				return nullptr;
			}

			g_ownedRendererIdentity = renderer;
			g_ownsRenderer.store(true, std::memory_order_release);
			g_loggedConflict = false;
			DiagnosticLog(
				"Acquired plugin rain renderer: renderer={:p} pluginRoot={:p} createdNow={}",
				static_cast<void*>(renderer),
				static_cast<void*>(g_geometry.get()),
				createdNow);
			return renderer;
		}

		void RelinquishLocked(const std::string_view a_reason)
		{
			StopImageSpaceModifier();
			if (!g_ownsRenderer.load(std::memory_order_acquire)) {
				return;
			}

			if (g_geometry) {
				g_geometry->SetAppCulled(true);
			}

			auto* const renderer = FindRenderer();
			if (renderer && renderer == g_ownedRendererIdentity && g_geometry &&
				renderer->screenAttachedElementRoot.get() == g_geometry.get()) {
				renderer->Disable();
				DiagnosticLog(
					"Disabled plugin rain renderer: renderer={:p} ({})",
					static_cast<void*>(renderer),
					a_reason);
			} else {
				REX::WARN(
					"Plugin rain renderer identity or root changed while active; released ownership without dereferencing the cached renderer: ownedRenderer={:p} liveRenderer={:p} ({})",
					static_cast<void*>(g_ownedRendererIdentity),
					static_cast<void*>(renderer),
					a_reason);
			}

			ClearOwnershipState();
		}

		void DetachGeometryFromRenderer()
		{
			if (!g_geometry) {
				return;
			}

			auto* const renderer = FindRenderer();
			if (renderer && renderer->screenAttachedElementRoot.get() == g_geometry.get()) {
				renderer->Disable();
				renderer->MainScreen_SetScreenAttached3D(nullptr);
				DiagnosticLog("Detached synthetic rain geometry for game-data teardown");
			}
		}
	}

	bool OwnsRenderer() noexcept
	{
		return g_ownsRenderer.load(std::memory_order_acquire);
	}

	void Start(const std::uint64_t a_time)
	{
		const std::scoped_lock lock{ g_stateLock };
		auto* const renderer = Acquire();
		if (!renderer) {
			return;
		}

		RE::NiUpdateData updateData{};
		updateData.time = static_cast<float>(a_time) * 0.001F;
		updateData.flags = 1;
		renderer->Enable(false);
		g_geometry->SetAppCulled(false);
		g_geometry->Update(updateData);

		if (!g_imageSpaceModifier) {
			auto* const modifier = Hooks::GetPowerArmorHUDRainModifier();
			if (!modifier) {
				if (!g_loggedMissingModifier) {
					REX::WARN("Could not resolve the Power Armor HUD rain image-space modifier");
					g_loggedMissingModifier = true;
				}
				return;
			}

			if (auto* instance = RE::ImageSpaceModifierInstanceForm::Trigger(
					modifier,
					1.0F,
					nullptr)) {
				g_imageSpaceModifier = instance;
				g_loggedMissingModifier = false;
				DiagnosticLog("Enabled rain outside Power Armor");
			}
		}
	}

	void Relinquish(const std::string_view a_reason)
	{
		const std::scoped_lock lock{ g_stateLock };
		RelinquishLocked(a_reason);
	}

	void Reset(const std::string_view a_reason, const bool a_releaseGeometry)
	{
		const std::scoped_lock lock{ g_stateLock };
		RelinquishLocked(a_reason);
		if (a_releaseGeometry) {
			DetachGeometryFromRenderer();
			g_geometry.reset();
			g_ownedRendererIdentity = nullptr;
			g_loggedConflict = false;
			g_loggedMissingModifier = false;
		}
	}
}
