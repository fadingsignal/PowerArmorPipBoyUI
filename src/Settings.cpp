#include "Settings.h"

#include "Diagnostics.h"

#include <SimpleIni.h>
#undef ERROR
#undef MAX_PATH

#include <fstream>
#include <iterator>
#include <string>

namespace PowerArmorPipBoyUI::Settings
{
	namespace
	{
		struct Config
		{
			bool forcePowerArmorPipboy = true;
			bool powerArmorAudio = true;
			bool keepPipboyLightOn = true;
			bool rainOverlayOutsidePowerArmor = false;
			bool usePipboyEffectColor = false;
			bool enableDebugLogging = false;
		};

		enum ConfigFlag : std::uint32_t
		{
			kForcePowerArmorPipboy = 1U << 0U,
			kPowerArmorAudio = 1U << 1U,
			kKeepPipboyLightOn = 1U << 2U,
			kRainOverlayOutsidePowerArmor = 1U << 3U,
			kUsePipboyEffectColor = 1U << 4U,
			kEnableDebugLogging = 1U << 5U,
		};

		[[nodiscard]] constexpr std::uint32_t EncodeConfig(const Config& a_config) noexcept
		{
			return (a_config.forcePowerArmorPipboy ? kForcePowerArmorPipboy : 0U) |
			       (a_config.powerArmorAudio ? kPowerArmorAudio : 0U) |
			       (a_config.keepPipboyLightOn ? kKeepPipboyLightOn : 0U) |
			       (a_config.rainOverlayOutsidePowerArmor ? kRainOverlayOutsidePowerArmor : 0U) |
			       (a_config.usePipboyEffectColor ? kUsePipboyEffectColor : 0U) |
			       (a_config.enableDebugLogging ? kEnableDebugLogging : 0U);
		}

		std::atomic_uint32_t g_configBits{ EncodeConfig(Config{}) };
		bool g_savedPowerArmorEffectColor = false;
		bool g_loggedMissingEffectColorSetting = false;
		std::array<float, 3> g_originalPowerArmorEffectColor{};

		void LogSettingsException(const char* a_message) noexcept
		{
			try {
				REX::ERROR("Could not reload PowerArmorPipBoyUI.ini: {}", a_message);
			} catch (...) {
			}
		}

		[[nodiscard]] std::filesystem::path GetFallbackIniPath()
		{
			std::error_code error;
			const auto currentPath = std::filesystem::current_path(error);
			if (!error) {
				return currentPath / "Data/F4SE/Plugins/PowerArmorPipBoyUI.ini";
			}

			REX::WARN(
				"Could not resolve the executable or current directory; using a relative INI path");
			return "Data/F4SE/Plugins/PowerArmorPipBoyUI.ini";
		}

		[[nodiscard]] std::filesystem::path GetIniPath()
		{
			constexpr std::size_t kMaximumWindowsPath = 32768;
			std::vector<wchar_t> executablePath(REX::W32::MAX_PATH);
			while (executablePath.size() <= kMaximumWindowsPath) {
				const auto length = REX::W32::GetModuleFileNameW(
					nullptr,
					executablePath.data(),
					static_cast<std::uint32_t>(executablePath.size()));
				if (length == 0) {
					break;
				}
				if (length < executablePath.size()) {
					return std::filesystem::path(executablePath.data()).parent_path() /
					       "Data/F4SE/Plugins/PowerArmorPipBoyUI.ini";
				}

				executablePath.resize(executablePath.size() * 2U);
			}

			REX::WARN("Could not resolve the Fallout 4 executable path; trying the current directory");
			return GetFallbackIniPath();
		}

		[[nodiscard]] bool ReadConfig(const std::filesystem::path& a_iniPath, Config& a_config)
		{
			std::ifstream stream(a_iniPath, std::ios::binary);
			if (!stream) {
				LogSettingsException("the INI could not be opened; keeping the previous settings");
				return false;
			}

			const std::string contents{
				std::istreambuf_iterator<char>{ stream },
				std::istreambuf_iterator<char>{}
			};
			if (stream.bad()) {
				LogSettingsException("the INI could not be read completely; keeping the previous settings");
				return false;
			}

			CSimpleIniA ini;
			ini.SetUnicode(true);
			ini.SetQuotes(true);
			if (ini.LoadData(contents.data(), contents.size()) < SI_OK) {
				LogSettingsException("the INI could not be parsed; keeping the previous settings");
				return false;
			}

			Config loaded{};
			loaded.forcePowerArmorPipboy =
				ini.GetBoolValue("General", "bForcePowerArmorPipboy", true);
			loaded.powerArmorAudio = ini.GetBoolValue("General", "bPowerArmorAudio", true);
			loaded.keepPipboyLightOn = ini.GetBoolValue("General", "bKeepPipboyLightOn", true);
			loaded.rainOverlayOutsidePowerArmor =
				ini.GetBoolValue("General", "bRainOverlayOutsidePowerArmor", false);
			loaded.usePipboyEffectColor =
				ini.GetBoolValue("General", "bUsePipboyEffectColor", false);
			loaded.enableDebugLogging =
				ini.GetBoolValue("General", "bEnableDebugLogging", false);
			a_config = loaded;
			return true;
		}

		[[nodiscard]] RE::Setting* GetFloatINISetting(const char* a_name)
		{
			auto* setting = RE::GetINISetting(a_name);
			return setting && setting->GetType() == RE::Setting::SETTING_TYPE::kFloat ?
			           setting :
			           nullptr;
		}

		void ApplyPipboyEffectColor(const bool a_usePipboyEffectColor)
		{
			if (!a_usePipboyEffectColor && !g_savedPowerArmorEffectColor) {
				return;
			}

			constexpr std::array pipboyColorNames{
				"fPipboyEffectColorR:Pipboy",
				"fPipboyEffectColorG:Pipboy",
				"fPipboyEffectColorB:Pipboy",
			};
			constexpr std::array powerArmorColorNames{
				"fPAEffectColorR:Pipboy",
				"fPAEffectColorG:Pipboy",
				"fPAEffectColorB:Pipboy",
			};

			std::array<RE::Setting*, 3> powerArmorSettings{};
			for (std::size_t i = 0; i < powerArmorSettings.size(); ++i) {
				powerArmorSettings[i] = GetFloatINISetting(powerArmorColorNames[i]);
				if (!powerArmorSettings[i]) {
					if (!g_loggedMissingEffectColorSetting) {
						REX::WARN(
							"Could not resolve {}; the Power Armor Pip-Boy effect color was not changed",
							powerArmorColorNames[i]);
						g_loggedMissingEffectColorSetting = true;
					}
					return;
				}
			}

			if (!a_usePipboyEffectColor) {
				if (g_savedPowerArmorEffectColor) {
					for (std::size_t i = 0; i < powerArmorSettings.size(); ++i) {
						powerArmorSettings[i]->SetFloat(g_originalPowerArmorEffectColor[i]);
					}
					DiagnosticLog(
						"Restored Power Armor Pip-Boy effect color: R={:.4f} G={:.4f} B={:.4f}",
						g_originalPowerArmorEffectColor[0],
						g_originalPowerArmorEffectColor[1],
						g_originalPowerArmorEffectColor[2]);
					g_savedPowerArmorEffectColor = false;
				}
				return;
			}

			std::array<float, 3> pipboyColor{};
			for (std::size_t i = 0; i < pipboyColor.size(); ++i) {
				const auto* setting = GetFloatINISetting(pipboyColorNames[i]);
				if (!setting || !std::isfinite(setting->GetFloat())) {
					if (!g_loggedMissingEffectColorSetting) {
						REX::WARN(
							"Could not read a finite value from {}; the Power Armor Pip-Boy effect color was not changed",
							pipboyColorNames[i]);
						g_loggedMissingEffectColorSetting = true;
					}
					return;
				}
				pipboyColor[i] = setting->GetFloat();
			}

			if (!g_savedPowerArmorEffectColor) {
				for (std::size_t i = 0; i < powerArmorSettings.size(); ++i) {
					g_originalPowerArmorEffectColor[i] = powerArmorSettings[i]->GetFloat();
				}
				g_savedPowerArmorEffectColor = true;
			}

			bool changed = false;
			for (std::size_t i = 0; i < powerArmorSettings.size(); ++i) {
				changed = changed || powerArmorSettings[i]->GetFloat() != pipboyColor[i];
				powerArmorSettings[i]->SetFloat(pipboyColor[i]);
			}
			if (changed) {
				DiagnosticLog(
					"Applied Fallout4Prefs.ini Pip-Boy effect color to the Power Armor Pip-Boy: R={:.4f} G={:.4f} B={:.4f}",
					pipboyColor[0],
					pipboyColor[1],
					pipboyColor[2]);
			}
			g_loggedMissingEffectColorSetting = false;
		}

		void LoadImpl()
		{
			const auto iniPath = GetIniPath();
			Config config{};
			if (!ReadConfig(iniPath, config)) {
				return;
			}

			static bool firstLoad = true;
			const auto configBits = EncodeConfig(config);
			const auto previousBits = g_configBits.exchange(configBits, std::memory_order_acq_rel);
			ApplyPipboyEffectColor(config.usePipboyEffectColor);

			if (config.enableDebugLogging && (firstLoad || previousBits != configBits)) {
				DiagnosticLog(
					"Loaded settings: bForcePowerArmorPipboy={} bPowerArmorAudio={} bKeepPipboyLightOn={} bRainOverlayOutsidePowerArmor={} bUsePipboyEffectColor={} bEnableDebugLogging={} ({})",
					config.forcePowerArmorPipboy,
					config.powerArmorAudio,
					config.keepPipboyLightOn,
					config.rainOverlayOutsidePowerArmor,
					config.usePipboyEffectColor,
					config.enableDebugLogging,
					iniPath.string());
			}
			firstLoad = false;
		}
	}

	void Load() noexcept
	{
		// This boundary is shared by plugin startup and the open/close trampoline
		// callbacks. Filesystem/path allocation failures must not unwind into Fallout 4.
		try {
			LoadImpl();
		} catch (const std::exception& exception) {
			LogSettingsException(exception.what());
		} catch (...) {
			LogSettingsException("unknown exception");
		}
	}

	void ReloadRainOverlaySetting() noexcept
	{
		try {
			const auto iniPath = GetIniPath();
			Config config{};
			if (!ReadConfig(iniPath, config)) {
				return;
			}

			auto previousBits = g_configBits.load(std::memory_order_acquire);
			std::uint32_t updatedBits{};
			do {
				updatedBits = config.rainOverlayOutsidePowerArmor ?
				                  previousBits | kRainOverlayOutsidePowerArmor :
				                  previousBits & ~kRainOverlayOutsidePowerArmor;
			} while (!g_configBits.compare_exchange_weak(
				previousBits,
				updatedBits,
				std::memory_order_acq_rel,
				std::memory_order_acquire));

			const bool previous = (previousBits & kRainOverlayOutsidePowerArmor) != 0;
			if (previous != config.rainOverlayOutsidePowerArmor) {
				DiagnosticLog(
					"Hot-reloaded bRainOverlayOutsidePowerArmor={} ({})",
					config.rainOverlayOutsidePowerArmor,
					iniPath.string());
			}
		} catch (const std::exception& exception) {
			LogSettingsException(exception.what());
		} catch (...) {
			LogSettingsException("unknown exception");
		}
	}

	bool ForcePowerArmorPipboy() noexcept
	{
		return (g_configBits.load(std::memory_order_acquire) & kForcePowerArmorPipboy) != 0;
	}

	bool PowerArmorAudio() noexcept
	{
		return (g_configBits.load(std::memory_order_acquire) & kPowerArmorAudio) != 0;
	}

	bool KeepPipboyLightOn() noexcept
	{
		return (g_configBits.load(std::memory_order_acquire) & kKeepPipboyLightOn) != 0;
	}

	bool RainOverlayOutsidePowerArmor() noexcept
	{
		return (g_configBits.load(std::memory_order_acquire) & kRainOverlayOutsidePowerArmor) != 0;
	}

	bool DebugLoggingEnabled() noexcept
	{
		return (g_configBits.load(std::memory_order_acquire) & kEnableDebugLogging) != 0;
	}
}
