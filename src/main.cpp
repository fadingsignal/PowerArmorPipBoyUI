#include "Hooks.h"
#include "Lifecycle.h"
#include "Runtime.h"
#include "Settings.h"

F4SE_PLUGIN_LOAD(const F4SE::LoadInterface* a_f4se)
{
	F4SE::Init(a_f4se, {
		.log = true,
		.logName = "PowerArmorPipBoyUI",
		.trampoline = true,
		.trampolineSize = 512,
	});

	const auto runtime = a_f4se->RuntimeVersion();
	if (!PowerArmorPipBoyUI::Runtime::IsSupported(runtime)) {
		REX::ERROR("Unsupported Fallout 4 runtime {}", runtime.string());
		return false;
	}

	PowerArmorPipBoyUI::Settings::Load();
	const auto hookAddresses = PowerArmorPipBoyUI::Runtime::ResolveHookAddresses(runtime);
	if (!hookAddresses) {
		return false;
	}
	if (!PowerArmorPipBoyUI::Lifecycle::Register()) {
		return false;
	}
	PowerArmorPipBoyUI::Hooks::Install(*hookAddresses);

	return true;
}

extern "C"
{
	F4SE_EXPORT bool F4SEPlugin_Query(
		const F4SE::QueryInterface* a_f4se,
		F4SE::PluginInfo* a_info)
	{
		if (!a_info) {
			return false;
		}

		const auto* version = F4SE::PluginVersionData::GetSingleton();
		a_info->name = version ? version->GetPluginName().data() : "PowerArmorPipBoyUI";
		a_info->infoVersion = F4SE::PluginInfo::kVersion;
		a_info->version = version ? version->GetPluginVersion().pack() : 1;

		return !a_f4se->IsEditor();
	}
}
