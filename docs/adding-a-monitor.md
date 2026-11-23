# Adding a monitor

Lumen controls any monitor that speaks DDC/CI, but it only knows the
standard controls (brightness, contrast, volume, color temperature) until
someone writes a **profile** for the model. A profile names the model's own
features: picture modes, backlights such as BenQ's MoonHalo, input
switching, and anything else the manufacturer put behind a VCP code.

If Lumen says your monitor has no profile yet, this is how to add one. You
need your monitor, its on-screen menu, and about half an hour.

## 1. Find the codes

Every monitor setting lives at a VCP code (0x00–0xFF). The standard ones are
documented in the VESA MCCS spec; manufacturers use the rest however they
like and rarely publish them. The reliable way to find one is to change the
setting with the monitor's own buttons and see which code moved.

```sh
lumen state | jq '.monitors[] | {connector, serial, model}'   # pick your monitor
lumen scan DP-2 > before.txt
# change ONE setting in the monitor's on-screen menu
lumen scan DP-2 > after.txt
diff before.txt after.txt
```

`lumen scan` only reads. Some codes drift on their own (timers, usage
counters), so repeat the change back and forth: the code you want follows
the setting every time. Code 0x52 just reports "the last thing that
changed" and is not a control.

## 2. Try writing it

```sh
lumen set DP-2 0xD7 0x11    # the code you found, and a value you saw in the diff
```

Watch the monitor. Writing to a single monitor sends exactly what you ask,
so be careful:

- Lumen never writes factory or color resets (0x04, 0x05, 0x06, 0x08, 0x0A).
- Some vendor codes do surprising things. On the BenQ RD320U, 0xE6 switches
  the monitor into DisplayPort daisy-chain mode and resets its USB hub. If
  a monitor disappears, power-cycle it or set the code back.
- If you find a code like that, list it under `avoid` in the profile so
  Lumen refuses to write it.

## 3. Write the profile

```sh
lumen profile DP-2
```

prints the path of a profile file for your model in
`~/.config/lumen/profiles/`, creating it with the standard controls and the
picture-mode values your monitor reports. Lumen reloads profiles as soon as
they're saved, so you can edit the file with the window or the bar panel
open and watch the controls appear.

```json
{
  "id": "benq-rd320u",
  "name": "BenQ RD320U",
  "match": [{ "make": "BNQ", "model": "BenQ RD320U" }],
  "controls": [
    { "id": "mode", "label": "Picture mode", "type": "choice", "code": "0xDC",
      "options": [{ "label": "Coding Dark", "value": "0x30" }] },
    { "id": "halo", "label": "MoonHalo", "type": "toggle", "code": "0xD7", "on": "0x21", "off": "0x11",
      "note": "Shown as a hint next to the control." },
    { "id": "brightness", "label": "Brightness", "type": "slider", "code": "0x10" },
    { "id": "temp", "label": "Color temperature", "type": "choice", "code": "0x14", "panel": false }
  ],
  "avoid": [{ "code": "0xE6", "reason": "Switches to DisplayPort MST and resets the USB hub." }]
}
```

| Field | |
|---|---|
| `id` | Lowercase model slug, also the file name (`benq-rd320u.json`). |
| `name` | Shown to people. |
| `match` | One or more `{make, model}` pairs, compared with what `lumen state` prints for the monitor. `make` is the three-letter EDID code (BNQ, DEL, GSM…); leave it out to match the model alone. |
| `controls` | In the order they should appear. |
| `controls[].id` | What `lumen set` takes. Reuse `brightness`, `contrast`, `volume`, `mode`, `temp` where they fit, so linked changes reach monitors of other models. |
| `controls[].type` | `slider` (continuous, 0 to the monitor's maximum), `choice` (a list of `options`; leave `options` out to use what the monitor reports), or `toggle` (`on` and `off` values). |
| `controls[].code` | The VCP code, as `"0xD7"` or a number. |
| `controls[].panel` | `false` keeps a control out of the Omarchy bar panel, which stays short; it's still in the window. |
| `controls[].note` | Optional hint. |
| `avoid` | Codes Lumen must never write, with a reason. |

Values can be numbers or hex strings. Choices and toggles only reach
monitors with the same profile when changes are linked, because their
values mean different things on different models; sliders reach every
monitor that has one with the same `id`.

## 4. Share it

1. Copy your file into `profiles/` in this repository.
2. Add it to `src/resources.qrc` beside the others, so it's built in.
3. Run `bin/test`. The `builtInProfilesLoadAndMatch` test fails on a
   profile that doesn't parse.
4. Open a pull request with the model, its firmware version if the menu
   shows one, and what you tested. Note anything you couldn't map, as the
   RD320U profile does for MoonHalo's 270°/360° shape, so the next person
   doesn't repeat the search.
