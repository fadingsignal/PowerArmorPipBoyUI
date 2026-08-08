#include "Settings.h"

#include "Diagnostics.h"

namespace PowerArmorPipBoyUI::Settings
{
	namespace
	{
		bool g_forcePowerArmorPipboy = true;
		bool g_powerArmorAudio = true;
		bool g_keepPipboyLightOn = true;
		bool g_usePipboyEffectColor = false;
		bool g_enableDebugLogging = false;
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

		[[nodiscard]] std::filesystem::path GetIniPath()
		{
			std::array<wchar_t, REX::W32::MAX_PATH> executablePath{};
			const auto length = REX::W32::GetModuleFileNameW(
				nullptr,
				executablePath.data(),
				static_cast<std::uint32_t>(executablePath.size()));

			if (length == 0 || length >= executablePath.size()) {
				std::error_code error;
				const auto currentPath = std::filesystem::current_path(error);
				if (!error) {
					return currentPath / "Data/F4SE/Plugins/PowerArmorPipBoyUI.ini";
				}

				REX::WARN(
					"Could not resolve the executable or current directory; using a relative INI path");
				return "Data/F4SE/Plugins/PowerArmorPipBoyUI.ini";
			}

			return std::filesystem::path(executablePath.data()).parent_path() /
			       "Data/F4SE/Plugins/PowerArmorPipBoyUI.ini";
		}

		[[nodiscard]] bool GetSetting(
			const std::filesystem::path& a_iniPath,
			const wchar_t* a_key,
			const bool a_default)
		{
			return REX::W32::GetPrivateProfileIntW(
				L"General",
				a_key,
				a_default ? 1 : 0,
				a_iniPath.c_str()) != 0;
		}

		[[nodiscard]] RE::Setting* GetFloatINISetting(const char* a_name)
		{
			auto* setting = RE::GetINISetting(a_name);
			return setting && setting->GetType() == RE::Setting::SETTING_TYPE::kFloat ?
			           setting :
			           nullptr;
		}

		void ApplyPipboyEffectColor()
		{
			if (!g_usePipboyEffectColor && !g_savedPowerArmorEffectColor) {
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

			if (!g_usePipboyEffectColor) {
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
			const bool forcePowerArmorPipboy = GetSetting(iniPath, L"bForcePowerArmorPipboy", true);
			const bool powerArmorAudio = GetSetting(iniPath, L"bPowerArmorAudio", true);
			const bool keepPipboyLightOn = GetSetting(iniPath, L"bKeepPipboyLightOn", true);
			const bool usePipboyEffectColor = GetSetting(iniPath, L"bUsePipboyEffectColor", false);
			const bool enableDebugLogging = GetSetting(iniPath, L"bEnableDebugLogging", false);

			static bool firstLoad = true;
			const bool changed =
				forcePowerArmorPipboy != g_forcePowerArmorPipboy ||
				powerArmorAudio != g_powerArmorAudio ||
				keepPipboyLightOn != g_keepPipboyLightOn ||
				usePipboyEffectColor != g_usePipboyEffectColor ||
				enableDebugLogging != g_enableDebugLogging;

			g_forcePowerArmorPipboy = forcePowerArmorPipboy;
			g_powerArmorAudio = powerArmorAudio;
			g_keepPipboyLightOn = keepPipboyLightOn;
			g_usePipboyEffectColor = usePipboyEffectColor;
			g_enableDebugLogging = enableDebugLogging;
			ApplyPipboyEffectColor();

			if (g_enableDebugLogging && (firstLoad || changed)) {
				DiagnosticLog(
					"Loaded settings: bForcePowerArmorPipboy={} bPowerArmorAudio={} bKeepPipboyLightOn={} bUsePipboyEffectColor={} bEnableDebugLogging={} ({})",
					g_forcePowerArmorPipboy,
					g_powerArmorAudio,
					g_keepPipboyLightOn,
					g_usePipboyEffectColor,
					g_enableDebugLogging,
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

	bool ForcePowerArmorPipboy() noexcept
	{
		return g_forcePowerArmorPipboy;
	}

	bool PowerArmorAudio() noexcept
	{
		return g_powerArmorAudio;
	}

	bool KeepPipboyLightOn() noexcept
	{
		return g_keepPipboyLightOn;
	}

	bool DebugLoggingEnabled() noexcept
	{
		return g_enableDebugLogging;
	}
}
