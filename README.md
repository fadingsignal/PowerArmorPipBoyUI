# Power Armor Pip-Boy UI

## Overview

Power Armor Pip-Boy UI is an F4SE/CommonLibF4 plugin for Fallout 4 that replaces
the wrist-mounted Pip-Boy UI with the instant, translucent fullscreen presentation
present when wearing Power Armor.

It also has an added bonus feature of showing rain drips on the screen outside of
Power Armor.

All options can be toggled on/off on the INI.

Requirements:

- Fallout 4 `1.10.163`
- F4SE `0.6.23`
- Address Library for F4SE Plugins with `version-1-10-163-0.bin`

## Installation

Install the DLL and `PowerArmorPipBoyUI.ini` in `Data/F4SE/Plugins`. The INI is
reloaded while the game is running, with presentation changes taking effect the
next time the Pip-Boy opens.

```ini
[General]
bForcePowerArmorPipboy=1
bPowerArmorAudio=1
bKeepPipboyLightOn=1
bRainOverlayOutsidePowerArmor=0
bUsePipboyEffectColor=0
bEnableDebugLogging=0
```

- `bForcePowerArmorPipboy` enables the fullscreen Pip-Boy outside power armor.
- `bPowerArmorAudio` uses the power-armor open and close sounds.
- `bKeepPipboyLightOn` preserves the Pip-Boy light while the menu is open.
- `bRainOverlayOutsidePowerArmor` enables the vanilla rain-on-glass effect
  outside power armor.
- `bUsePipboyEffectColor` uses the player's Pip-Boy color instead of the fixed
  power-armor orange.
- `bEnableDebugLogging` enables verbose diagnostic logging. Warnings and errors
  are always logged.


## Technical Detail

The plugin reuses Fallout 4's existing `PipboyMenu` movie and
`"PipboyMenu"` Interface3D renderer. It redirects only the relevant Pip-Boy
power-armor checks, loads `Interface/Objects/PADashPipboyScreen.nif` through
`BSModelDB`, and attaches the renderer to that camera-aligned screen. If the
screen cannot be loaded, it falls back to the vanilla wrist presentation.

Forced opens use the engine's no-animation path because the normal player
behavior graph cannot emit the power-armor animation events. Genuine
power-armor use remains vanilla. Close paths still converge on the engine's
`ClosedownPipboy` routine so menu, cursor, input, light, and deferred-action
state are restored normally.

Runtime-specific hook sites and targets are validated before any patches are
written. F4SE lifecycle registration also completes before hook installation.
Presentation state, input policy, screen geometry, terminal handoff, rain
policy, and renderer ownership are separated into focused modules. Settings
are parsed into a complete snapshot and published atomically; a missing or
invalid INI leaves the previous settings active.

The optional rain overlay loads an independent instance of the native wet-armor
geometry and temporarily leases `HUDRainRenderer`. It follows vanilla weather,
interior, submersion, and camera rules, yields to genuine power armor, and is
hidden during loading screens.

Building requires xmake 3.0.0 or newer and a C++23-capable Visual Studio 2022
toolchain. Clone the repository recursively, then run:

```powershell
git submodule update --init --recursive
xmake f -m releasedbg
xmake
xmake install -o dist
```

The DLL, PDB, and INI are packaged under `dist/F4SE/Plugins`. Keep the explicit
`-o dist` argument so environment variables cannot redirect the package into a
game or mod-manager directory. Dependency resolutions are pinned in
`xmake-requires.lock`.

## Credits

- Bethesda Game Studios for Fallout 4.
- Ian Patterson, Stephen Abel, and Brendan Borthwick for F4SE.
- Ryan-rsm-McKenzie and contributors for CommonLibF4 and Address Library.
- Brodie Thiesfield (brofield) for SimpleIni.
- Gabi Melman (gabime) and contributors for spdlog.
- Ruki Wang (waruqi) and contributors for xmake.
- The Ghidra team, Doodlez for Bethesda Ghidra Scripts, and Benjamin Ethington
  (bethington) for Ghidra-MCP.
- shad0wshayd3 for BakaPowerArmorHUD and other Fallout 4 research projects.
