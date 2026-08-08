# Power Armor Pip-Boy UI

An F4SE/CommonLibF4 plugin that presents Fallout 4's Pip-Boy menu the way it
appears in power armor — an instant, translucent fullscreen overlay — without
raising or displaying the wrist-mounted Pip-Boy.

The initial prototype supports Fallout 4 `1.10.163` only. It uses the Address
Library to resolve each containing function and validates every instruction and
target before writing anything.

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
bPowerArmorAudio=0
bKeepPipboyLightOn=0
```

Settings are read when the plugin loads. Set `bForcePowerArmorPipboy` to `0` to
restore the normal wrist-mounted Pip-Boy presentation.

## Open paths

Both the Tab/Pip-Boy input handler and the Pip-Boy companion app's use-item
command are redirected to the instant no-animation open. Terminal interaction
is deliberately left alone because `TerminalMenu` is a separate presentation.

While a forced menu is open, its native input handler recognizes the Pip-Boy
toggle and queues the standard `PipboyMenu` hide message. This preserves the
vanilla menu teardown while avoiding any dependency on the wrist animation
graph for closing.

## Building

The project expects CommonLibF4 in the sibling directory `../commonlibf4`.

```powershell
xmake f -m releasedbg
xmake
```
