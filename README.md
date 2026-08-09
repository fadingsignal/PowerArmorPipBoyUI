# Power Armor Pip-Boy UI

An F4SE/CommonLibF4 plugin that presents Fallout 4's Pip-Boy menu the way it
appears in power armor — an instant, translucent fullscreen overlay — without
raising or displaying the wrist-mounted Pip-Boy.

The initial prototype supports Fallout 4 `1.10.163` only. It uses the Address
Library for most containing functions and validated runtime-specific offsets for
a small number of call sites that have no reliable library ID. Every instruction
and target is checked before the plugin writes anything.

## Requirements

- Fallout 4 `1.10.163`
- F4SE `0.6.23`
- Address Library for F4SE Plugins containing `version-1-10-163-0.bin`

## How it works

Both presentations use the same `PipboyMenu` Scaleform movie and the same
`"PipboyMenu"` Interface3D renderer. The only difference is what the movie is
composited onto, chosen by `PipboyManager::RefreshPipboyRenderSurface`:

- Outside power armor the renderer is *world-attached* to the screen mesh on the
  player's raised wrist.
- In power armor it is *screen-attached* to `pipboyPAGlass`, a camera-aligned
  quad loaded from `Interface/Objects/PADashPipboyScreen.nif`, with
  `useFullPremultAlpha` set and the scanline effect disabled.

`PipboyManager` reaches that decision through a series of `Actor::IsInPowerArmor`
calls; the plugin redirects only its Pip-Boy call sites so the power-armor branch
is taken. It loads `PADashPipboyScreen.nif` directly through `BSModelDB`, without
initializing the Power Armor dashboard, rain plane, or PA animation graph. The
decision is latched before each open so the viewport, renderer, shader, camera,
menu hit testing, and close path all see the same presentation. Matching the
menu's hit-test geometry to the screen-attached quad is required for cursor and
button interaction. If the screen cannot be loaded, the plugin falls back to
the wrist presentation rather than rendering an empty screen.

The normal player behavior graph delays the menu until it emits the
`pipboyOpened` event after the wrist-raise animation. For forced opens the plugin
uses the engine's generic no-animation path, which proceeds directly to the same
Pip-Boy open-completion handler. Genuine Power Armor opens keep their vanilla
behavior graph.

This does not touch `PowerArmorHUDMenu` — the helmet dials, battery gauge and
vignette are a separate menu and never appear.

## Configuration

Install `PowerArmorPipBoyUI.ini` beside the DLL in `Data/F4SE/Plugins`:

```ini
[General]
bForcePowerArmorPipboy=1
bPowerArmorAudio=1
bKeepPipboyLightOn=1
bRainOverlayOutsidePowerArmor=0
bUsePipboyEffectColor=0
bEnableDebugLogging=0
```

Settings are reloaded whenever the Pip-Boy opens and before a forced close, so
editing the INI does not require restarting Fallout 4. Presentation changes take
effect on the next open; an audio change made while the menu is open can affect
its close sound.

Set `bRainOverlayOutsidePowerArmor` to `1` to show Fallout 4's vanilla
Power Armor rain-on-glass effect while outside Power Armor. The plugin uses an
independently loaded instance of the native wet-armor geometry and temporarily leases the
vanilla `HUDRainRenderer`; genuine Power Armor retains ownership of its normal
effect. Weather transitions, interiors, submersion, and both first- and
third-person cameras follow the native behavior.

Set `bUsePipboyEffectColor` to `1` to use the player's
`fPipboyEffectColorR/G/B` values from `Fallout4Prefs.ini` in place of the Power
Armor Pip-Boy's fixed `fPAEffectColorR/G/B` orange. This affects both genuine
and forced Power Armor Pip-Boy presentations. `bPipboyDisableFX` continues to
control whether Fallout 4 renders the Pip-Boy scanline/effect pass at all.

Set `bForcePowerArmorPipboy` to `0` to restore the normal wrist-mounted Pip-Boy
presentation on the next open.

Set `bEnableDebugLogging` to `1` to record verbose menu-transition, geometry,
camera, holotape, color, and live-settings diagnostics. Errors, warnings, and
the startup hook-install summary are always logged. The default is `0` for a
quieter release log; enable it before reproducing an issue when additional
diagnostic detail is useful.

## Open paths

Both the Tab/Pip-Boy input handler and the Pip-Boy companion app's use-item
command are redirected to the instant no-animation open. Interacting with a
world terminal is deliberately left alone; only a `TerminalMenu` launched by a
holotape inside an active forced Pip-Boy receives the PA presentation decisions.

While a forced menu is open, the plugin admits the Pip-Boy toggle at the stable
top level and completes the PA close event synchronously. This avoids waiting
for an event that the non-PA animation graph cannot produce. Item inspection,
modal prompts, holotape games, and terminal-form holotapes retain ownership of
Cancel/Tab until their native unwind has finished, so the parent Pip-Boy is not
closed beneath a nested input layer.

Holotape loads use the engine's native no-animation path during a forced
presentation. Pip-Boy games continue through `PipboyHolotapeMenu`; terminal-form
holotapes continue through `TerminalMenu`, with its PA render-target dimensions
and hit-test projection selected so framing and mouse input match the fullscreen
quad. Genuine power armor keeps its vanilla holotape behavior.

All final-close paths converge on the engine's `ClosedownPipboy` routine. The
plugin resets its forced state and detaches the standalone screen only after
that routine restores menu, cursor, input, light, and deferred-action state.
Load and new-game messages provide an additional defensive reset. Settings and
filesystem failures are contained at the reload boundary rather than unwinding
through an engine hook; the previous/default settings remain available and the
failure is logged.

## Building

Building requires xmake 3.0.0 or newer and a C++23-capable Visual Studio 2022
toolchain.

CommonLibF4 is pinned in the `extern/commonlibf4` submodule. Clone the
repository recursively, or initialize the dependency in an existing checkout:

```powershell
git submodule update --init --recursive
```

```powershell
xmake f -m releasedbg
xmake
xmake install -o dist
```

The packaged DLL, PDB, and INI are written beneath `dist/F4SE/Plugins`.
Keep the explicit `-o dist` argument: it prevents CommonLibF4's optional
`XSE_FO4_MODS_PATH` or `XSE_FO4_GAME_PATH` environment variables from sending
the package to a mod-manager or game directory instead.
