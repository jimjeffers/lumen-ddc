# Lumen

Monitor settings over DDC/CI, from one Omarchy window, the command line, or
the [Omarchy bar](https://github.com/jimjeffers/omarchy-lumen). Every monitor
gets brightness, contrast, volume and color temperature; models with a
profile also get their own features, such as BenQ's picture modes and
MoonHalo backlight. Monitors are listed left to right as Hyprland arranges
them, and changes can apply to all of them at once ("Link monitors").

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
resets (VCP 0x04, 0x05, 0x06, 0x08, 0x0A) are never sent, nor are codes a
profile lists under `avoid`.

DDC traffic through some Thunderbolt hubs makes the monitor pulse its hotplug
line, after which Hyprland can leave the output off. After each burst of
traffic Lumen checks, and reloads Hyprland's config if a monitor it manages
was switched off.

## Architecture

Nothing in the code knows about a particular monitor. Model knowledge lives
in profiles, and everything else works from what a profile says.

```
Hyprland ──► Backend ──► Monitor ──► ddcutil ──► the monitor's I2C bus
 (which        │  (one per   │ (one queue,
 monitors,     │  screen)    │  reads and writes
 left→right)   │             │  by VCP code)
               ▼             ▼
            Profiles ──► controls: [{id, label, type, code, value, options, …}]
       built-in + ~/.config      │
                                 ├─► the window (MonitorCard.qml)
                                 ├─► lumen state / lumen set (JSON, for scripts)
                                 └─► the bar plugin (reads lumen state)
```

| File | Role |
|---|---|
| `profiles/*.json` | One file per model: which controls it has, their VCP codes and values, and codes to never write. `generic.json` is the fallback for models without one. |
| `src/profile.{h,cpp}` | Parses and validates profiles, loads the built-in ones (compiled in from `src/resources.qrc`) and the user's, and matches a monitor to one by EDID make and model. |
| `src/monitor.{h,cpp}` | One monitor: a single ddcutil queue, the values it last read, and its profile's controls with live values. Refuses resets and avoided codes. |
| `src/backend.{h,cpp}` | Finds monitors through Hyprland, gives each its profile, applies changes by control id (to all monitors when linked), reloads profiles when their files change, and turns back on an output Hyprland switched off. |
| `src/cli.{h,cpp}` | `state`, `set`, `scan` and `profile`, without a window. |
| `src/ddc.{h,cpp}` | Pure parsing of ddcutil and Hyprland output. |
| `src/*.qml` | The window. A monitor card draws whatever controls its profile lists. |

Controls come in three types: `slider` (brightness, volume…), `choice` (a
list of values, such as picture modes) and `toggle` (on and off values, such
as a backlight). Because the window, the CLI and the bar plugin only see this
list, a new profile shows up in all three with no code changes.

Linking follows the control type: a slider change reaches every monitor with
a control of the same id, while choices and toggles only reach monitors with
the same profile, since their values mean different things on different
models.

## Adding a monitor

Most of the work is finding which VCP codes your monitor uses for its own
features, and Lumen has tools for that:

1. `lumen scan DP-2 > before.txt`, change one setting on the monitor's menu,
   `lumen scan DP-2 > after.txt`, and `diff` the two.
2. `lumen set DP-2 0xNN 0xVV` to check the code does what you think.
3. `lumen profile DP-2` to start a profile for the model in
   `~/.config/lumen/profiles/`. Lumen reloads it on every save, so the
   controls appear in the window and the bar as you write it.
4. Copy the file into `profiles/`, list it in `src/resources.qrc`, run
   `bin/test`, and open a pull request.

[docs/adding-a-monitor.md](docs/adding-a-monitor.md) has the details: the
profile format, codes that drift on their own, and codes to be careful with.

## Build

```sh
bin/build     # build/lumen
bin/test      # unit tests, offscreen
bin/install   # makepkg -fsi: installs lumen and its launcher entry
```

Needs `ddcutil`, Qt 6 (base, declarative, wayland) and Hyprland.
