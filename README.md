# Lumen

Brightness, contrast, color temperature, picture mode and speaker volume for
external monitors, over DDC/CI, from one Omarchy window. Monitors are listed
left to right as Hyprland arranges them, and changes can apply to all of them
at once ("Link monitors").

## Supported monitors

Every DDC/CI monitor gets the standard controls: brightness, contrast,
speaker volume and color temperature. A **profile** adds a model's own
features. Built in:

| Monitor | Extra controls |
|---|---|
| BenQ RD320U | Picture modes, MoonHalo on/off |

Profiles are small JSON files in [`profiles/`](profiles). Adding your
monitor takes about half an hour with its on-screen menu:
[docs/adding-a-monitor.md](docs/adding-a-monitor.md). Files in
`~/.config/lumen/profiles/` add models or override a built-in profile and
apply as soon as they're saved.

## Install

```sh
git clone https://github.com/jimjeffers/lumen
cd lumen && bin/install
```

DDC/CI has to be on in each monitor's on-screen menu.

## Bar widget

[omarchy-lumen](https://github.com/jimjeffers/omarchy-lumen) puts the same
controls in the Omarchy bar:

```sh
omarchy plugin add https://github.com/jimjeffers/omarchy-lumen --enable
```

## Command line

```sh
lumen state                      # every monitor's settings and controls as JSON
lumen set all brightness 40      # all monitors
lumen set DP-2 mode coding-dark  # one monitor, by connector or serial
lumen set DP-2 halo off          # toggles take on/off
lumen scan DP-2                  # every VCP code the monitor answers, for mapping
lumen profile DP-2               # create or find the monitor's profile file
```

## Shortcuts

| Key | Action |
| --- | --- |
| ↑ / ↓ | Brightness up / down |
| Shift + ↑ / ↓ | Contrast up / down |
| ← / → | Select monitor |
| Space / L | Link or unlink monitors |
| R | Reread settings |
| ? | Show or hide shortcuts |
| Q | Quit |

## How it talks to monitors

Lumen runs `ddcutil` against each monitor's own I2C bus (found from the DRM
connector), never its slow detection pass. Each monitor has one queue, so
requests never overlap, and dragging a slider sends only the latest value.
Capabilities are read once and cached in `~/.cache/lumen/`. Factory and color
resets (VCP 0x04, 0x08) are never sent.

DDC traffic through some Thunderbolt hubs makes the monitor pulse its hotplug
line, after which Hyprland can leave the output off. After each burst of
traffic Lumen checks, and reloads Hyprland's config if a monitor it manages
was switched off.

## Build

```sh
bin/build     # build/lumen
bin/test      # unit tests, offscreen
bin/install   # makepkg -fsi: installs lumen and its launcher entry
```

Needs `ddcutil`, Qt 6 (base, declarative, wayland) and Hyprland.
