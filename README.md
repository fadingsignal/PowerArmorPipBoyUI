# Power Armor Pip-Boy UI

## Overview

Power Armor Pip-Boy UI is an F4SE/CommonLibF4 plugin for Fallout 4 that replaces
the wrist-mounted Pip-Boy UI with the instant, translucent fullscreen presentation
present when wearing Power Armor.

It also has an added bonus feature of showing rain drips on the screen outside of
Power Armor.

All options can be toggled on or off in the INI at runtime.

## Compatibility and requirements

**One DLL supports all three of these Fallout 4 runtimes:**

| Runtime | Required Address Library file |
| --- | --- |
| `1.10.163` (OG) | `version-1-10-163-0.bin` |
| `1.11.221` (AE) | `version-1-11-221-0.bin` |
| `1.11.240` (AE) | `version-1-11-240-0.bin` |

Install the F4SE build and Address Library for F4SE Plugins database that match
your exact game runtime. There are no separate OG and AE versions of this DLL.

In-game testing on all three runtimes has covered Pip-Boy use, holotapes,
terminals, rain, genuine power armor, and save/load. Static binary audits and
regression checks also pass for all three profiles.

Other runtimes, including `1.11.137`, NG `1.10.980`/`1.10.984`, and Fallout 4 VR,
are not currently supported. New or older runtimes can be added after their
mappings and behavior are verified. An Address Library update alone does not
automatically enable an unlisted runtime.

## Installation

Install `PowerArmorPipBoyUI.dll` and `PowerArmorPipBoyUI.ini` in
`Data/F4SE/Plugins`, alongside the matching Address Library file. Launch the
game through F4SE. The PDB file is optional and provides debugging symbols.

When updating, preserve your existing INI if you want to keep your settings.

## Configuration

The INI is reloaded when the Pip-Boy opens, so presentation settings can be
changed without restarting the game. Use `1` to enable an option and `0` to
disable it. The supplied INI currently contains:

```ini
[General]
bForcePowerArmorPipboy=1
bPowerArmorAudio=1
bKeepPipboyLightOn=1
bRainOverlayOutsidePowerArmor=1
bUsePipboyEffectColor=1
bEnableDebugLogging=1
```

- `bForcePowerArmorPipboy` enables the fullscreen Pip-Boy outside power armor.
- `bPowerArmorAudio` uses the power-armor open and close sounds.
- `bKeepPipboyLightOn` preserves the Pip-Boy light while the menu is open.
- `bRainOverlayOutsidePowerArmor` enables the vanilla rain-on-glass effect
  outside power armor.
- `bUsePipboyEffectColor` uses the player's Pip-Boy color instead of the fixed
  power-armor orange.
- `bEnableDebugLogging` enables verbose diagnostic logging. Set it to `0` for
  quieter logs; warnings and errors are always logged.

## Technical details

The plugin reuses Fallout 4's existing `PipboyMenu` movie and
`"PipboyMenu"` Interface3D renderer. It redirects only the relevant Pip-Boy
power-armor checks, loads `Interface/Objects/PADashPipboyScreen.nif` through
`BSModelDB`, and attaches the renderer to that camera-aligned screen. If the
screen cannot be loaded, it falls back to the vanilla wrist presentation.

Forced opens use the engine's no-animation path because the normal player
behavior graph cannot emit the power-armor animation events. Genuine
power-armor use retains the engine's normal presentation and lifecycle. Close
cleanup hooks run after the complete `OnPipboyClosed` function, allowing the
engine to restore menu, cursor, input, light, and deferred-action state before
the plugin resets its forced presentation. This boundary works across the
different OG and AE close implementations.

The compatibility database is `src/Runtime/Profiles.h`. It records exact game
versions, Address Library IDs, internal call offsets, expected targets, vtable
slots, helper ABIs, and renderer setup validation data. The `.240` profile
shares the verified `.221` mappings while using its own Address Library file.
The DLL's runtime gate and F4SE compatibility metadata use the same registry.

Address Library locates engine functions and tables; the plugin also depends
on instructions inside those functions, vtable slots, and object layouts.
Before installation, it validates the complete required manifest, including
call opcodes and targets, executable sections, original vtable entries,
overlapping call patches, and the renderer setup instruction context. A failed
check prevents hook installation. There are no whole-function prologue detours
or signature scans.

F4SE lifecycle registration also completes before hook installation.
Presentation state, input policy, screen geometry, terminal handoff, rain
policy, and renderer ownership are separated into focused modules. Settings
are parsed into a complete snapshot and published atomically; a missing or
invalid INI leaves the previous settings active.

The optional rain overlay loads an independent instance of the native wet-armor
geometry and temporarily leases `HUDRainRenderer`. It follows vanilla weather,
interior, submersion, and camera rules, yields to genuine power armor, and is
hidden during loading screens.

## Building

Building requires xmake 3.0.0 or newer and a C++23-capable Visual Studio 2022
toolchain. Clone the repository recursively, then run:

```powershell
git submodule update --init --recursive
xmake f -m releasedbg -y
xmake build PowerArmorPipBoyUI
xmake install -o dist PowerArmorPipBoyUI
```

The DLL, PDB, and INI are packaged under `dist/F4SE/Plugins`. Keep the explicit
`-o dist` argument so environment variables cannot redirect the package into a
game or mod-manager directory. Dependency resolutions are pinned in
`xmake-requires.lock`.

## Auditing runtime compatibility

The optional `runtime-audit` tool reads an executable and its matching Address
Library without running or modifying the game. It shares the DLL's profiles
and byte validators, and also checks containing x64 unwind-function ranges.

```powershell
xmake build runtime-audit
& .\build\windows\x64\releasedbg\runtime-audit.exe 'PATH_TO_GAME\Fallout4.exe' 'PATH_TO_PLUGINS\version-1-11-240-0.bin' --regression
```

Use the actual paths and matching version filename for the runtime being
audited. A packed executable may require an unpacked analysis copy.
`--regression` also checks rejection of invalid hook locations, changed setup
context, incorrect callees, and unregistered versions.

To compare another executable against the `.221` baseline, replace
`--regression` with `--compare 1.11.221.0`. Comparison does not register the
version or enable it in the DLL. A passing audit provides static evidence;
ABI, layout, caller, and in-game checks are still required before adding support.

## License

PowerArmorPipBoyUI is licensed under the GNU General Public License version 3
(GPL-3.0). See [LICENSE](LICENSE) for the full terms.

Third-party dependencies retain their own licenses and notices. In particular,
`commonlib-shared` is distributed under GPL v3 with the additional permissions
in its [EXCEPTIONS](extern/commonlibf4/lib/commonlib-shared/EXCEPTIONS) file.

## Credits

- Bethesda Game Studios for Fallout 4.
- Ian Patterson, Stephen Abel, and Brendan Borthwick for F4SE.
- Ryan-rsm-McKenzie and contributors for CommonLibF4 and Address Library.
- Brodie Thiesfield (brofield) for SimpleIni.
- Gabi Melman (gabime) and contributors for spdlog.
- Ruki Wang (waruqi) and contributors for xmake.
- The Ghidra team, Doodlez and 1001Bits for Bethesda Ghidra Scripts, and Benjamin Ethington
  (bethington) for Ghidra-MCP.
- shad0wshayd3 for BakaPowerArmorHUD and other Fallout 4 research projects.
- Zzyxzz for additional runtime address databases
