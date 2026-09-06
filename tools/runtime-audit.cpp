// Read-only audit of the exact manifest compiled into the DLL.
// This tool never loads, runs, or modifies Fallout4.exe.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <winver.h>

#include "Runtime/Validation.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace Profiles = PowerArmorPipBoyUI::Runtime::Profiles;
namespace Validation = PowerArmorPipBoyUI::Runtime::Validation;

namespace
{
using Bytes = std::span<const std::uint8_t>;

template <class T> T Read(Bytes a_bytes, std::uint64_t a_offset)
{
	if (!Validation::Contains(0, a_bytes.size(), a_offset, sizeof(T))) {
		throw std::runtime_error("Truncated input or invalid file offset");
	}
	T value;
	std::memcpy(&value, a_bytes.data() + a_offset, sizeof(value));
	return value;
}

std::vector<std::uint8_t> Load(const std::filesystem::path& a_path)
{
	std::ifstream input(a_path, std::ios::binary | std::ios::ate);
	if (!input || input.tellg() < 0) {
		throw std::runtime_error("Cannot read " + a_path.string());
	}
	std::vector<std::uint8_t> bytes(static_cast<std::size_t>(input.tellg()));
	input.seekg(0);
	if (!input.read(reinterpret_cast<char*>(bytes.data()),
	                static_cast<std::streamsize>(bytes.size()))) {
		throw std::runtime_error("Could not read complete file: " + a_path.string());
	}
	return bytes;
}

Profiles::Version FileVersion(const std::filesystem::path& a_path)
{
	DWORD unused = 0;
	const auto size = GetFileVersionInfoSizeW(a_path.c_str(), &unused);
	std::vector<std::uint8_t> data(size);
	if (!size || !GetFileVersionInfoW(a_path.c_str(), 0, size, data.data())) {
		throw std::runtime_error("Executable has no readable version resource");
	}
	VS_FIXEDFILEINFO* info = nullptr;
	UINT length = 0;
	if (!VerQueryValueW(data.data(), L"\\", reinterpret_cast<void**>(&info), &length) ||
	    length < sizeof(VS_FIXEDFILEINFO) || info->dwSignature != 0xFEEF04BD) {
		throw std::runtime_error("Invalid executable version resource");
	}
	return {HIWORD(info->dwFileVersionMS), LOWORD(info->dwFileVersionMS),
	        HIWORD(info->dwFileVersionLS), LOWORD(info->dwFileVersionLS)};
}

std::string VersionString(const Profiles::Version& a_version, char a_separator = '.')
{
	return std::to_string(a_version[0]) + a_separator + std::to_string(a_version[1]) + a_separator +
	       std::to_string(a_version[2]) + a_separator + std::to_string(a_version[3]);
}

class AddressLibrary
{
  public:
	explicit AddressLibrary(const std::filesystem::path& a_path) : data(Load(a_path))
	{
		count = Read<std::uint64_t>(data, 0);
		if ((data.size() - 8) % 16 || count != (data.size() - 8) / 16) {
			throw std::runtime_error(
				"Expected F4SE v0 Address Library: count + (uint64 ID, uint64 RVA) records");
		}
		for (std::uint64_t i = 1; i < count; ++i) {
			if (Read<std::uint64_t>(data, 8 + (i - 1) * 16) >=
			    Read<std::uint64_t>(data, 8 + i * 16)) {
				throw std::runtime_error("Address Library IDs are not strictly increasing");
			}
		}
	}

	std::uint64_t Resolve(std::uint64_t a_id) const
	{
		std::uint64_t first = 0, last = count;
		while (first < last) {
			const auto middle = first + (last - first) / 2;
			if (Read<std::uint64_t>(data, 8 + middle * 16) < a_id) {
				first = middle + 1;
			}
			else {
				last = middle;
			}
		}
		if (first == count || Read<std::uint64_t>(data, 8 + first * 16) != a_id) {
			throw std::runtime_error("Missing Address Library ID " + std::to_string(a_id));
		}
		return Read<std::uint64_t>(data, 16 + first * 16);
	}

  private:
	std::vector<std::uint8_t> data;
	std::uint64_t count;
};

class Executable
{
  public:
	explicit Executable(const std::filesystem::path& a_path) : data(Load(a_path))
	{
		const auto dos = Read<IMAGE_DOS_HEADER>(data, 0);
		if (dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew < 0) {
			throw std::runtime_error("Invalid DOS header");
		}
		const auto nt = Read<IMAGE_NT_HEADERS64>(data, static_cast<std::uint64_t>(dos.e_lfanew));
		if (nt.Signature != IMAGE_NT_SIGNATURE ||
		    nt.FileHeader.Machine != IMAGE_FILE_MACHINE_AMD64 ||
		    nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC ||
		    nt.FileHeader.SizeOfOptionalHeader < sizeof(IMAGE_OPTIONAL_HEADER64) ||
		    nt.OptionalHeader.NumberOfRvaAndSizes <= IMAGE_DIRECTORY_ENTRY_EXCEPTION) {
			throw std::runtime_error("Expected an x64 PE executable");
		}
		base = nt.OptionalHeader.ImageBase;
		const auto sectionOffset = static_cast<std::uint64_t>(dos.e_lfanew) + 4 +
		                           sizeof(IMAGE_FILE_HEADER) + nt.FileHeader.SizeOfOptionalHeader;
		for (std::size_t i = 0; i < nt.FileHeader.NumberOfSections; ++i) {
			sections.push_back(
				Read<IMAGE_SECTION_HEADER>(data, sectionOffset + i * sizeof(IMAGE_SECTION_HEADER)));
		}
		const auto& exception = nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];
		pdata = View(exception.VirtualAddress, exception.Size, ".pdata");
		if (pdata.empty() || pdata.size() % sizeof(RUNTIME_FUNCTION)) {
			throw std::runtime_error("Missing or invalid x64 unwind function table");
		}
	}

	Bytes View(std::uint64_t a_rva, std::uint64_t a_size, std::string_view a_section) const
	{
		for (const auto& section : sections) {
			const std::string_view name(
				reinterpret_cast<const char*>(section.Name),
				strnlen(reinterpret_cast<const char*>(section.Name), IMAGE_SIZEOF_SHORT_NAME));
			if ((a_section.empty() || name == a_section) &&
			    Validation::Contains(section.VirtualAddress, section.Misc.VirtualSize, a_rva,
			                         a_size) &&
			    Validation::Contains(section.VirtualAddress, section.SizeOfRawData, a_rva,
			                         a_size) &&
			    (a_section != ".text" || (section.Characteristics & IMAGE_SCN_MEM_EXECUTE)) &&
			    (a_section != ".rdata" || (section.Characteristics & IMAGE_SCN_MEM_READ))) {
				const auto offset = section.PointerToRawData + (a_rva - section.VirtualAddress);
				if (Validation::Contains(0, data.size(), offset, a_size)) {
					return Bytes(data).subspan(static_cast<std::size_t>(offset),
					                           static_cast<std::size_t>(a_size));
				}
			}
		}
		return {};
	}

	bool FunctionContains(std::uint64_t a_function, std::uint64_t a_site,
	                      std::uint64_t a_length) const
	{
		for (std::size_t i = 0; i < pdata.size(); i += sizeof(RUNTIME_FUNCTION)) {
			auto entry = Read<RUNTIME_FUNCTION>(pdata, i);
			if (entry.EndAddress <= entry.BeginAddress ||
			    !Validation::Contains(entry.BeginAddress, entry.EndAddress - entry.BeginAddress,
			                          a_site, a_length)) {
				continue;
			}
			// One C++ function can have several chained unwind ranges. Follow the
			// chain to its actual entry rather than treating each range as a function.
			// https://learn.microsoft.com/en-us/cpp/build/exception-handling-x64
			for (unsigned depth = 0; depth < 32; ++depth) {
				if (entry.BeginAddress == a_function) {
					return true;
				}
				const auto header = View(entry.UnwindData, 4, {});
				if (header.size() != 4 || !(header[0] & (4 << 3))) {
					break;
				}
				const auto chainOffset =
					4 + ((static_cast<std::uint32_t>(header[2]) + 1) & ~1U) * 2;
				entry = Read<RUNTIME_FUNCTION>(
					View(entry.UnwindData, chainOffset + sizeof(RUNTIME_FUNCTION), {}),
					chainOffset);
			}
		}
		return false;
	}

	std::uint64_t base{};

  private:
	std::vector<std::uint8_t> data;
	std::vector<IMAGE_SECTION_HEADER> sections;
	Bytes pdata;
};

void Require(bool a_ok, std::string_view a_message)
{
	if (!a_ok) {
		throw std::runtime_error(std::string(a_message));
	}
}

void RegressionChecks(const Executable& a_exe, const AddressLibrary& a_library,
                      const Profiles::Profile& a_profile)
{
	const auto setup = std::find_if(a_profile.calls.begin(), a_profile.calls.end(),
	                                [](const auto& site) { return site.rendererSetup; });
	Require(setup != a_profile.calls.end(), "No renderer setup regression fixture");
	const auto function = a_library.Resolve(setup->containingID);
	const auto target = a_library.Resolve(setup->targetID);
	const auto context =
		a_exe.View(function, a_profile.setup.branchOffset + a_profile.setup.branch.size(), ".text");
	std::vector<std::uint8_t> changed(context.begin(), context.end());
	Require(Validation::SetupMatches(changed, setup->offset, a_profile.setup),
	        "Valid setup context rejected");
	Require(!Validation::SetupMatches(changed, setup->offset + 1, a_profile.setup),
	        "Invalid delta accepted");
	Require(!Validation::DirectCallMatches(a_exe.View(function + setup->offset + 1, 5, ".text"),
	                                       function + setup->offset + 1, target),
	        "Invalid call-site delta accepted");
	changed[setup->offset + 5] ^= 1;
	Require(!Validation::SetupMatches(changed, setup->offset, a_profile.setup),
	        "Changed result handling accepted");
	changed.assign(context.begin(), context.end());
	changed[a_profile.setup.branchOffset + 4] ^= 1;
	Require(!Validation::SetupMatches(changed, setup->offset, a_profile.setup),
	        "Changed PA branch accepted");
	const auto call = a_exe.View(function + setup->offset, 5, ".text");
	std::vector<std::uint8_t> tail(call.begin(), call.end());
	tail[0] = 0xE9;
	Require(!Validation::DirectCallMatches(tail, function + setup->offset, target),
	        "Tail jump accepted as call");
	Require(!Validation::DirectCallMatches(call, function + setup->offset, target + 1),
	        "Wrong callee accepted");
	Require(!Validation::DirectCallMatches(call.first(4), function + setup->offset, target),
	        "Truncated call accepted");
	Require(!Validation::Contains(100, 10, 109, 5), "Section overrun accepted");
	Require(!Validation::Contains(100, 10, 99, 1), "Section underrun accepted");
	for (const auto version : {Profiles::Version{1, 10, 980, 0}, Profiles::Version{1, 10, 984, 0},
	                           Profiles::Version{1, 11, 137, 0}, Profiles::Version{1, 12, 0, 0}}) {
		Require(!Profiles::Find(version), "Unverified runtime accepted");
	}
	for (const auto* profile : Profiles::kProfiles) {
		Require(Profiles::Find(profile->version) == profile,
		        "Registered runtime not selected exactly");
	}
	if (a_profile.version == Profiles::kAE221.version ||
	    a_profile.version == Profiles::kAE240.version) {
		const auto wrongFunction = a_library.Resolve(2225483);
		Require(Validation::DirectCallMatches(a_exe.View(wrongFunction + 0x0B, 5, ".text"),
		                                      wrongFunction + 0x0B, target),
		        "Historical inverse-helper fixture no longer calls ActorInPowerArmor");
		Require(
			!Validation::SetupMatches(
				a_exe.View(wrongFunction,
		                   a_profile.setup.branchOffset + a_profile.setup.branch.size(), ".text"),
				0x0B, a_profile.setup),
			"Historical inverse-helper bug accepted");
	}
	std::cout << "PASS negative checks: invalid delta, inverse/setup context, branch, tail jump, "
				 "callee, bounds, version gate\n";
}
} // namespace

int wmain(int argc, wchar_t** argv)
{
	try {
		const bool regression = argc == 4 && std::wstring_view(argv[3]) == L"--regression";
		const bool comparison = argc == 5 && std::wstring_view(argv[3]) == L"--compare";
		if (argc != 3 && !regression && !comparison) {
			std::cerr << "Usage: runtime-audit <Fallout4.exe> <version-X-X-X-X.bin> [--regression "
						 "| --compare 1.11.221.0]\n";
			return 2;
		}
		const std::filesystem::path executablePath(argv[1]), libraryPath(argv[2]);
		const auto version = FileVersion(executablePath);
		const auto* profile = Profiles::Find(version);
		if (comparison) {
			profile = nullptr;
			for (const auto* candidate : Profiles::kProfiles) {
				const auto name = VersionString(candidate->version);
				if (std::wstring(name.begin(), name.end()) == argv[4]) {
					profile = candidate;
					break;
				}
			}
			Require(profile != nullptr, "Unknown baseline profile for comparison");
		}
		Require(profile != nullptr, "No verified profile for executable " + VersionString(version));
		Require(libraryPath.filename().string() ==
		            "version-" + VersionString(version, '-') + ".bin",
		        "Address Library filename does not match executable version (v0 has no embedded "
		        "version)");
		const Executable exe(executablePath);
		const AddressLibrary library(libraryPath);
		std::cout << "Profile: " << profile->name << "\n";
		if (comparison) {
			std::cout << "COMPARISON ONLY against executable " << VersionString(version)
					  << "; this does not enable runtime support.\n";
		}
		unsigned failures = 0;
		const auto check = [&failures](std::string_view label, const auto& action) {
			try {
				action();
				std::cout << "PASS " << label << "\n";
			}
			catch (const std::exception& error) {
				++failures;
				std::cerr << "FAIL " << label << ": " << error.what() << "\n";
			}
		};
		std::vector<std::uint64_t> occupied;
		for (const auto& site : profile->calls) {
			check(site.description, [&] {
				const auto function = library.Resolve(site.containingID),
						   target = library.Resolve(site.targetID);
				Require(!exe.View(function, static_cast<std::uint64_t>(site.offset) + 5, ".text")
				                .empty() &&
				            !exe.View(target, 1, ".text").empty(),
				        "Function or callee outside executable .text");
				const auto address = function + site.offset;
				Require(exe.FunctionContains(function, address, 5),
				        "Call outside containing unwind function");
				Require(
					Validation::DirectCallMatches(exe.View(address, 5, ".text"), address, target),
					"Opcode/callee mismatch at ID " + std::to_string(site.containingID) + " + " +
						std::to_string(site.offset));
				for (const auto previous : occupied) {
					Require(!(address < previous + 5 && previous < address + 5),
					        "Overlapping patches");
				}
				occupied.push_back(address);
				if (site.rendererSetup) {
					const auto size = profile->setup.branchOffset + profile->setup.branch.size();
					Require(exe.FunctionContains(function, function, size),
					        "Setup guard exceeds function");
					Require(Validation::SetupMatches(exe.View(function, size, ".text"), site.offset,
					                                 profile->setup),
					        "Renderer setup context mismatch");
				}
			});
		}
		for (const auto& site : profile->vtables) {
			check(site.description, [&] {
				const auto table = library.Resolve(site.tableID),
						   target = library.Resolve(site.targetID);
				Require(!exe.View(target, 1, ".text").empty(),
				        "Vtable target outside executable .text");
				const auto slot =
					exe.View(table, (site.slot + 1) * sizeof(std::uint64_t), ".rdata");
				Require(Read<std::uint64_t>(slot, site.slot * sizeof(std::uint64_t)) ==
				            exe.base + target,
				        "Vtable original entry mismatch");
			});
		}
		const auto& ids = profile->functions;
		for (const auto id : {ids.actorInPowerArmor, ids.activeSetter, ids.rainModifier,
		                      ids.referenceIsInterior, ids.submergeLevel}) {
			check("callable ID " + std::to_string(id), [&] {
				Require(!exe.View(library.Resolve(id), 1, ".text").empty(),
				        "Callable outside executable .text");
			});
		}
		if (!failures && regression) {
			check("regression suite", [&] { RegressionChecks(exe, library, *profile); });
		}
		std::cout << profile->calls.size() << " calls, " << profile->vtables.size()
				  << " vtables, 5 callable locations; " << failures << " failure(s).\n"
				  << "Static evidence only: ABI, object layouts, caller completeness, and in-game "
					 "behavior still need review.\n";
		return failures ? 1 : 0;
	}
	catch (const std::exception& error) {
		std::cerr << "ERROR: " << error.what() << "\n";
		return 2;
	}
}
