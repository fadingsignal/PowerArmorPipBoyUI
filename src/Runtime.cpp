#include "Runtime.h"
#include "Runtime/Validation.h"

namespace PowerArmorPipBoyUI::Runtime
{
namespace
{
[[nodiscard]] const Profiles::Profile* FindProfile(const REL::Version& a_runtime)
{
	return Profiles::Find({a_runtime[0], a_runtime[1], a_runtime[2], a_runtime[3]});
}

[[nodiscard]] bool InSection(const REX::FModuleSection& a_section, std::uintptr_t a_address,
                             std::size_t a_size)
{
	return Validation::Contains(a_section.GetAddress(), a_section.GetSize(), a_address, a_size);
}

[[nodiscard]] std::span<const std::uint8_t> ReadCode(const REX::FModuleSection& a_text,
                                                     std::uintptr_t a_address, std::size_t a_size)
{
	if (!InSection(a_text, a_address, a_size)) {
		return {};
	}
	return {reinterpret_cast<const std::uint8_t*>(a_address), a_size};
}
} // namespace

bool IsSupported(const REL::Version& a_runtime) { return FindProfile(a_runtime) != nullptr; }

std::optional<HookAddresses> ResolveHookAddresses(const REL::Version& a_runtime)
{
	const auto* profile = FindProfile(a_runtime);
	if (!profile) {
		REX::ERROR("No verified hook/layout profile for Fallout 4 {}; refusing to patch",
		           a_runtime.string());
		return std::nullopt;
	}

	const auto module = REX::FModule::GetExecutingModule();
	const auto text = module.GetSection(".text");
	const auto rdata = module.GetSection(".rdata");
	HookAddresses addresses{};
	HookAddresses::Rain rain{};
	std::vector<std::uintptr_t> openCalls;
	std::vector<std::uintptr_t> installedSites;
	REX::INFO("Selected exact runtime profile {}", profile->name);

	for (const auto& site : profile->calls) {
		const auto function = REL::ID(site.containingID).address();
		const auto target = REL::ID(site.targetID).address();
		if (!InSection(text, function, static_cast<std::size_t>(site.offset) + 5) ||
		    !InSection(text, target, 1)) {
			REX::ERROR(
				"{}: ID {} + 0x{:X} or target ID {} outside executable .text; refusing to patch",
				site.description, site.containingID, site.offset, site.targetID);
			return std::nullopt;
		}
		const auto address = function + site.offset;
		if (!Validation::DirectCallMatches(ReadCode(text, address, 5), address, target)) {
			REX::ERROR(
				"{}: expected CALL to ID {} at ID {} + 0x{:X} (RVA 0x{:X}); refusing to patch",
				site.description, site.targetID, site.containingID, site.offset,
				address - module.GetBaseAddress());
			return std::nullopt;
		}
		if (site.rendererSetup &&
		    !Validation::SetupMatches(
				ReadCode(text, function,
		                 profile->setup.branchOffset + profile->setup.branch.size()),
				site.offset, profile->setup)) {
			REX::ERROR("{}: renderer setup context mismatch at ID {} + 0x{:X}; refusing to patch",
			           site.description, site.containingID, site.offset);
			return std::nullopt;
		}
		for (const auto existing : installedSites) {
			if (address < existing + 5 && existing < address + 5) {
				REX::ERROR("{}: overlapping call-site patches; refusing to patch",
				           site.description);
				return std::nullopt;
			}
		}
		installedSites.push_back(address);
		switch (site.kind) {
		case Profiles::CallKind::kPresentation:
			addresses.presentation.push_back(address);
			break;
		case Profiles::CallKind::kOpenAudio:
			addresses.openAudio.push_back(address);
			break;
		case Profiles::CallKind::kCloseAudio:
			addresses.closeAudio.push_back(address);
			break;
		case Profiles::CallKind::kLight:
			addresses.light.push_back(address);
			break;
		case Profiles::CallKind::kClosed:
			addresses.pipboyClosedCalls.push_back(address);
			break;
		case Profiles::CallKind::kOpen:
			openCalls.push_back(address);
			break;
		case Profiles::CallKind::kClose:
			addresses.pipboyCloseCalls.push_back(address);
			break;
		case Profiles::CallKind::kHolotape:
			addresses.pipboyLoadHolotapeCalls.push_back(address);
			break;
		}
	}
	if (openCalls.size() != 2) {
		REX::ERROR("Profile must provide Tab and companion open calls; refusing to patch");
		return std::nullopt;
	}
	addresses.tabHandlerCall = openCalls[0];
	addresses.companionUseItemCall = openCalls[1];

	for (const auto& site : profile->vtables) {
		const auto table = REL::ID(site.tableID).address();
		const auto target = REL::ID(site.targetID).address();
		if (site.slot >= rdata.GetSize() / sizeof(std::uintptr_t) ||
		    !InSection(rdata, table, (site.slot + 1) * sizeof(std::uintptr_t)) ||
		    !InSection(text, target, 1)) {
			REX::ERROR(
				"{}: vtable or function outside expected executable sections; refusing to patch",
				site.description);
			return std::nullopt;
		}
		const auto* entries = reinterpret_cast<const std::uintptr_t*>(table);
		if (entries[site.slot] != target) {
			REX::ERROR("{}: ID {} slot {} does not target ID {}; refusing to patch",
			           site.description, site.tableID, site.slot, site.targetID);
			return std::nullopt;
		}
		const HookAddresses::VtableHook hook{table, site.slot};
		switch (site.kind) {
		case Profiles::VtableKind::kPipboyFrame:
			addresses.pipboyFrame = hook;
			break;
		case Profiles::VtableKind::kInputShouldHandle:
			addresses.inputShouldHandle = hook;
			break;
		case Profiles::VtableKind::kInputButton:
			addresses.inputButton = hook;
			break;
		case Profiles::VtableKind::kFirstPersonUpdate:
			addresses.firstPersonUpdate = hook;
			break;
		case Profiles::VtableKind::kHUDFrame:
			rain.hudFrame = hook;
			break;
		}
	}

	const auto& ids = profile->functions;
	addresses.actorInPowerArmor = REL::ID(ids.actorInPowerArmor).address();
	addresses.setPipboyActive = HookAddresses::PipboyActiveSetter{
		REL::ID(ids.activeSetter).address(), profile->activeSetterABI};
	rain.powerArmorHUDRainModifierGetter = REL::ID(ids.rainModifier).address();
	rain.referenceIsInterior = REL::ID(ids.referenceIsInterior).address();
	rain.getSubmergeLevel = REL::ID(ids.submergeLevel).address();
	for (const auto function :
	     {addresses.actorInPowerArmor, addresses.setPipboyActive->address,
	      rain.powerArmorHUDRainModifierGetter, rain.referenceIsInterior, rain.getSubmergeLevel}) {
		if (!InSection(text, function, 1)) {
			REX::ERROR("Profile callable 0x{:X} outside executable .text; refusing to patch",
			           function);
			return std::nullopt;
		}
	}
	addresses.rain = rain;

	REX::INFO(
		"Validated complete {} profile: {} call sites, {} vtable slots, renderer setup context",
		profile->name, profile->calls.size(), profile->vtables.size());
	return addresses;
}
} // namespace PowerArmorPipBoyUI::Runtime
