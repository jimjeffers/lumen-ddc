# Lumen

Brightness, contrast, color temperature, picture mode and speaker volume for
external monitors, over DDC/CI, from one Omarchy window. Monitors are listed
left to right as Hyprland arranges them, and changes can apply to all of them
at once ("Link monitors").

Model-specific settings, such as BenQ's picture modes and MoonHalo backlight,
get names from a small file per model (`Names…` in the window creates it):

```ini
# ~/.config/lumen/benq-rd320u.conf
mode.coding-dark=0x30
halo.code=0xNN
halo.on=0x01
halo.off=0x00
```

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
lumen state                      # every monitor's settings as JSON
lumen set all brightness 40      # all monitors
lumen set DP-2 mode coding-dark  # one monitor, by connector or serial
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
