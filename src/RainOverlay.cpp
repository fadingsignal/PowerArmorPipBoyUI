#include "RainOverlay.h"

#include "Hooks.h"
#include "Rain/RendererLease.h"
#include "Settings.h"

namespace PowerArmorPipBoyUI::RainOverlay
{
	namespace
	{
		constexpr float kMaximumSubmergeLevel = 1.1F;
		constexpr float kWeatherByteScale = 0.003921569F;
		constexpr float kWeatherTransitionScale = 0.999F;
		constexpr float kWeatherTransitionEpsilon = 0.001F;

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

		bool g_gameDataReady = true;
		std::optional<WeatherSnapshot> g_weatherSnapshot;

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

		void Reset(const std::string_view a_reason, const bool a_releaseGeometry)
		{
			Rain::RendererLease::Reset(a_reason, a_releaseGeometry);
			g_weatherSnapshot.reset();
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
		if (enteringOrInPowerArmor && Rain::RendererLease::OwnsRenderer()) {
			Rain::RendererLease::Relinquish("genuine Power Armor update"sv);
		}

		Hooks::HUDMenuAdvanceMovie(a_menu, a_timeDelta, a_time);
		ReloadSettingOnWeatherEdge();

		const auto* player = RE::PlayerCharacter::GetSingleton();
		if (!g_gameDataReady || LoadingScreenOpen() ||
			!Settings::RainOverlayOutsidePowerArmor() || !player ||
			Hooks::ActorInPowerArmor(*player)) {
			if (Rain::RendererLease::OwnsRenderer()) {
				Rain::RendererLease::Relinquish("rain presentation not eligible"sv);
			}
			return;
		}

		if (ShouldShowRain(*player)) {
			Rain::RendererLease::Start(a_time);
		} else if (Rain::RendererLease::OwnsRenderer()) {
			Rain::RendererLease::Relinquish("rain conditions ended"sv);
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
