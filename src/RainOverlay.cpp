#include "RainOverlay.h"

#include "Diagnostics.h"
#include "Hooks.h"
#include "Settings.h"

namespace PowerArmorPipBoyUI::RainOverlay
{
	namespace
	{
		constexpr auto kRendererName = "HUDRainRenderer"sv;
		constexpr auto kRainModelPath = "Effects/CameraAttachedFX/CameraAttach_WetArmorView.nif"sv;
		constexpr auto kHUDGlassMaterial = "Materials\\Interface\\HUDGlassFlat.BGEM"sv;
		constexpr float kRainScale = 0.57F;
		constexpr RE::NiPoint3 kRainTranslation{ 0.0F, 351.0F, 0.0F };
		constexpr float kMaximumSubmergeLevel = 1.1F;
		constexpr float kWeatherByteScale = 0.003921569F;
		constexpr float kWeatherTransitionScale = 0.999F;
		constexpr float kWeatherTransitionEpsilon = 0.001F;
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

		struct WeatherSnapshot
		{
			const RE::TESWeather* currentWeather;
			const RE::TESWeather* lastWeather;
			const RE::TESWeather* overrideWeather;
			std::uint8_t currentFlags;
			std::uint8_t lastFlags;
			bool activeHUDRain;

			bool operator==(const WeatherSnapshot&) const = default;
		};

		RE::NiPointer<RE::NiNode> g_rainGeometry;
		RE::NiPointer<RE::NiAVObject> g_displacedRendererRoot;
		RE::NiPointer<RE::ImageSpaceModifierInstanceForm> g_imageSpaceModifier;
		RE::Interface3D::Renderer* g_renderer = nullptr;
		std::optional<RendererState> g_displacedRendererState;
		bool g_gameDataReady = true;
		bool g_ownsRendererRoot = false;
		bool g_loggedRendererConflict = false;
		bool g_loggedMissingModifier = false;
		std::optional<WeatherSnapshot> g_weatherSnapshot;

		[[nodiscard]] RE::BSModelDB::DBTraits::ArgsType MakeRainLoadArgs()
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

		[[nodiscard]] bool EnsureRainGeometry()
		{
			if (g_rainGeometry) {
				return true;
			}

			RE::NiPointer<RE::NiNode> rainGeometry;
			const auto args = MakeRainLoadArgs();
			const auto result = RE::BSModelDB::Demand(
				kRainModelPath.data(),
				std::addressof(rainGeometry),
				args);
			if (result != RE::BSResource::ErrorCode::kNone || !rainGeometry) {
				REX::ERROR(
					"Could not load {} (BSResource error {})",
					kRainModelPath,
					std::to_underlying(result));
				return false;
			}

			// Demand already creates an independently controlled scene instance.
			// Rendering that instance directly matches PowerArmorGeometry and keeps
			// the wet-armor particle/controller chain intact.
			g_rainGeometry = std::move(rainGeometry);
			g_rainGeometry->SetLocalScale(kRainScale);
			g_rainGeometry->SetLocalTranslate(kRainTranslation);
			RE::NiUpdateData updateData{};
			g_rainGeometry->Update(updateData);
			g_rainGeometry->SetAppCulled(true);
			DiagnosticLog(
				"Loaded independent WetArmorView instance={:p}",
				static_cast<void*>(g_rainGeometry.get()));
			return true;
		}

		[[nodiscard]] RendererState CaptureRendererState(const RE::Interface3D::Renderer& a_renderer)
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

		void ConfigureRenderer(RE::Interface3D::Renderer& a_renderer)
		{
			a_renderer.MainScreen_SetScreenAttached3D(g_rainGeometry.get());
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

		void RestoreRendererState(RE::Interface3D::Renderer& a_renderer, const RendererState& a_state)
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

		void StopImageSpaceModifier()
		{
			if (g_imageSpaceModifier) {
				static_cast<RE::ImageSpaceModifierInstance*>(g_imageSpaceModifier.get())->Stop();
				g_imageSpaceModifier.reset();
			}
		}

		void RelinquishRenderer(const std::string_view a_reason)
		{
			StopImageSpaceModifier();
			if (!g_ownsRendererRoot) {
				return;
			}

			if (g_rainGeometry) {
				g_rainGeometry->SetAppCulled(true);
			}

			if (g_renderer && g_rainGeometry &&
				g_renderer->screenAttachedElementRoot.get() == g_rainGeometry.get()) {
				g_renderer->Disable();
				g_renderer->MainScreen_SetScreenAttached3D(g_displacedRendererRoot.get());
				if (g_displacedRendererState) {
					RestoreRendererState(*g_renderer, *g_displacedRendererState);
				}
				DiagnosticLog("Restored HUDRainRenderer root ({})", a_reason);
			} else {
				REX::WARN(
					"HUDRainRenderer root changed while leased; released ownership without overwriting it ({})",
					a_reason);
			}

			g_ownsRendererRoot = false;
			g_displacedRendererRoot.reset();
			g_displacedRendererState.reset();
		}

		[[nodiscard]] bool AcquireRenderer()
		{
			if (g_ownsRendererRoot) {
				if (g_renderer && g_rainGeometry &&
					g_renderer->screenAttachedElementRoot.get() == g_rainGeometry.get()) {
					return true;
				}
				RelinquishRenderer("renderer ownership lost"sv);
				return false;
			}
			if (!EnsureRainGeometry()) {
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
			const auto* geometry = RE::PowerArmorGeometry::GetSingleton();
			auto* const vanillaRoot = geometry ? geometry->dbHUDRain.get() : nullptr;
			if (currentRoot && currentRoot != vanillaRoot && currentRoot != g_rainGeometry.get()) {
				if (!g_loggedRendererConflict) {
					REX::WARN(
						"HUDRainRenderer has an unexpected root {:p}; rain outside Power Armor is disabled until it becomes available",
						static_cast<void*>(currentRoot));
					g_loggedRendererConflict = true;
				}
				return false;
			}

			g_displacedRendererRoot = currentRoot;
			g_displacedRendererState = CaptureRendererState(*g_renderer);
			ConfigureRenderer(*g_renderer);
			if (g_renderer->screenAttachedElementRoot.get() != g_rainGeometry.get()) {
				REX::ERROR("HUDRainRenderer rejected the plugin rain geometry");
				g_displacedRendererRoot.reset();
				g_displacedRendererState.reset();
				return false;
			}

			g_ownsRendererRoot = true;
			g_loggedRendererConflict = false;
			DiagnosticLog(
				"Leased HUDRainRenderer: pluginRoot={:p} displacedRoot={:p}",
				static_cast<void*>(g_rainGeometry.get()),
				static_cast<void*>(g_displacedRendererRoot.get()));
			return true;
		}

		[[nodiscard]] bool WeatherHasFlag(
			const RE::TESWeather& a_weather,
			const RE::TESWeather::WeatherDataFlags a_flag)
		{
			const auto flags = static_cast<std::uint8_t>(
				a_weather.weatherData[std::to_underlying(RE::TESWeather::WeatherData::kFlags)]);
			return (flags & std::to_underlying(a_flag)) != 0;
		}

		[[nodiscard]] float WeatherThreshold(
			const RE::TESWeather& a_weather,
			const RE::TESWeather::WeatherData a_value)
		{
			return static_cast<float>(static_cast<std::uint8_t>(
				       a_weather.weatherData[std::to_underlying(a_value)])) *
			       kWeatherByteScale * kWeatherTransitionScale;
		}

		[[nodiscard]] bool IsActivelyRaining(const RE::Sky& a_sky)
		{
			const bool currentRaining =
				a_sky.currentWeather &&
				WeatherHasFlag(*a_sky.currentWeather, RE::TESWeather::WeatherDataFlags::kRainy) &&
				a_sky.currentWeatherPct >
					WeatherThreshold(*a_sky.currentWeather, RE::TESWeather::WeatherData::kBeginPrecip);
			const bool lastRaining =
				a_sky.lastWeather &&
				WeatherHasFlag(*a_sky.lastWeather, RE::TESWeather::WeatherDataFlags::kRainy) &&
				a_sky.currentWeatherPct <
					WeatherThreshold(*a_sky.lastWeather, RE::TESWeather::WeatherData::kEndPrecip) +
						kWeatherTransitionEpsilon;
			return currentRaining || lastRaining;
		}

		[[nodiscard]] std::uint8_t WeatherFlags(const RE::TESWeather* a_weather)
		{
			return a_weather ?
			           static_cast<std::uint8_t>(a_weather->weatherData[
						   std::to_underlying(RE::TESWeather::WeatherData::kFlags)]) :
			           0;
		}

		[[nodiscard]] WeatherSnapshot CaptureWeatherSnapshot()
		{
			const auto* sky = RE::Sky::GetSingleton();
			if (!sky) {
				return {};
			}

			const bool activeHUDRain =
				sky->currentWeather &&
				WeatherHasFlag(
					*sky->currentWeather,
					RE::TESWeather::WeatherDataFlags::kHudRain) &&
				IsActivelyRaining(*sky);
			return {
				sky->currentWeather,
				sky->lastWeather,
				sky->overrideWeather,
				WeatherFlags(sky->currentWeather),
				WeatherFlags(sky->lastWeather),
				activeHUDRain,
			};
		}

		void ReloadSettingOnWeatherEdge()
		{
			const auto snapshot = CaptureWeatherSnapshot();
			if (!g_weatherSnapshot || *g_weatherSnapshot != snapshot) {
				Settings::ReloadRainOverlaySetting();
				g_weatherSnapshot = snapshot;
			}
		}

		[[nodiscard]] bool ShouldShowRain(const RE::PlayerCharacter& a_player)
		{
			const auto* sky = RE::Sky::GetSingleton();
			if (!sky || !sky->currentWeather ||
				!WeatherHasFlag(*sky->currentWeather, RE::TESWeather::WeatherDataFlags::kHudRain) ||
				!IsActivelyRaining(*sky) || Hooks::ReferenceIsInterior(a_player)) {
				return false;
			}

			const float submergeLevel = Hooks::GetSubmergeLevel(a_player);
			return std::isfinite(submergeLevel) && submergeLevel < kMaximumSubmergeLevel;
		}

		[[nodiscard]] bool LoadingScreenOpen()
		{
			const auto* ui = RE::UI::GetSingleton();
			return ui && ui->GetMenuOpen<RE::LoadingMenu>();
		}

		void StartRain(const std::uint64_t a_time)
		{
			if (!AcquireRenderer()) {
				return;
			}

			RE::NiUpdateData updateData{};
			updateData.time = static_cast<float>(a_time) * 0.001F;
			updateData.flags = 1;
			g_renderer->Enable(false);
			g_rainGeometry->SetAppCulled(false);
			g_rainGeometry->Update(updateData);

			if (auto* modifier = Hooks::GetPowerArmorHUDRainModifier()) {
				// Vanilla calls Trigger every rainy frame. The form instance can expire,
				// leave the image-space manager, and then be recreated for the next
				// droplet cycle. Retaining the first instance must not suppress that
				// per-frame trigger attempt.
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

		void Reset(const std::string_view a_reason, const bool a_releaseGeometry)
		{
			RelinquishRenderer(a_reason);
			g_weatherSnapshot.reset();
			if (a_releaseGeometry) {
				g_rainGeometry.reset();
				g_renderer = nullptr;
				g_loggedRendererConflict = false;
				g_loggedMissingModifier = false;
			}
		}
	}

	void AdvanceHUDMenu(
		RE::IMenu* a_menu,
		const float a_timeDelta,
		const std::uint64_t a_time)
	{
		const auto* playerBefore = RE::PlayerCharacter::GetSingleton();
		const bool enteringOrInPowerArmor =
			playerBefore && Hooks::ActorInPowerArmor(*playerBefore);
		if (enteringOrInPowerArmor && g_ownsRendererRoot) {
			// Give the shared renderer and IMOD back before vanilla's PA update runs.
			RelinquishRenderer("genuine Power Armor update"sv);
		}

		Hooks::HUDMenuAdvanceMovie(a_menu, a_timeDelta, a_time);
		ReloadSettingOnWeatherEdge();

		const auto* player = RE::PlayerCharacter::GetSingleton();
		if (!g_gameDataReady || LoadingScreenOpen() ||
			!Settings::RainOverlayOutsidePowerArmor() || !player ||
			Hooks::ActorInPowerArmor(*player)) {
			if (g_ownsRendererRoot) {
				RelinquishRenderer("rain presentation not eligible"sv);
			}
			return;
		}

		if (ShouldShowRain(*player)) {
			StartRain(a_time);
		} else if (g_ownsRendererRoot) {
			RelinquishRenderer("rain conditions ended"sv);
		}
	}

	void OnF4SEMessage(F4SE::MessagingInterface::Message* a_message)
	{
		if (a_message->type == F4SE::MessagingInterface::kPreLoadGame) {
			g_gameDataReady = false;
			Reset("pre-load game"sv, true);
			return;
		}

		if (a_message->type == F4SE::MessagingInterface::kGameDataReady) {
			g_gameDataReady = a_message->data != nullptr;
			if (!g_gameDataReady) {
				Reset("game data unloaded"sv, true);
			}
			return;
		}

		if (a_message->type == F4SE::MessagingInterface::kPostLoadGame ||
			a_message->type == F4SE::MessagingInterface::kNewGame) {
			g_gameDataReady = true;
			Reset(
				a_message->type == F4SE::MessagingInterface::kPostLoadGame ?
					"post-load game"sv :
					"new game"sv,
				true);
		}
	}
}
