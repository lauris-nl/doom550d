# Doom 550D v0.3.1

This release targets the Canon EOS 550D with firmware 1.0.9 and retains the
playable game, input, save and audio behavior from v0.3.0.

## Added debug option to ML's game menu

- Enable or disable Doom debug logging from `Games > Doom > Debug logging`.
- Keep logging disabled by default for normal play and enable it before
  reproducing a problem.
- Persist the selected logging state in Magic Lantern's module configuration.
- Remove `ML/LOGS/DOOM*.LOG` safely with `Games > Doom > Clear Doom logs`.
- Refuse log deletion while Doom is active.
- Leave WADs, savegames and Doom configuration untouched when clearing logs.

## Highlights

- Play MUS music through a richer 24-voice, 24 kHz low-CPU synthesizer.
- Mix music and Doom's original 8-bit effects into the 48 kHz camera output.
- Use independent in-game music and sound-effect volume controls.
- Hold zoom in (`+`) to run.
- Hold zoom out (`-`) while pressing left/right to strafe.
- Save directly with SET while retaining separate saves for every IWAD.

## Stability and input fixes

- Validate WAD cache ownership after loading saves, avoiding stale cache
  pointers and the `Z_ChangeTag: block without a ZONEID` module crash.
- Reset queued camera input after a successful save, preventing controls from
  becoming stuck while music continues.
- Intercept Delete/Trash as Doom Escape so it cannot open a hidden Magic
  Lantern menu over the game.
- Avoid ISO, depth-of-field and shutter controls because Canon can process
  them before the module and display EOS overlays or steal input.
- Preserve display, palette, input and audio cleanup across repeated runs.
- Retain the commercial MAP01 intermission-name bounds fix.

## Music backend

The synthesizer supports 24 voices, multiple inexpensive waveforms, General
MIDI instrument families, percussion, envelopes, channel volume and pitch
bend. It is designed to provide an AdLib-like retro sound without the CPU cost
that made the earlier cycle-level OPL2 experiments unstable on the 550D. It is
not cycle-accurate OPL2 emulation.

When debug logging is enabled, audio timing is recorded in
`ML/LOGS/DOOM550D.LOG`. The tested game build reported no missed audio
deadlines during successful gameplay sessions.

## Installation and compatibility

- Target: Canon EOS 550D / Rebel T2i / Kiss X4, firmware 1.0.9 only.
- Install as `ML/MODULES/doom.mo`.
- Use a matching Magic Lantern `autoexec.bin` and `550D_109.sym`.
- Copy legally obtained Doom-compatible IWADs to `ML/DOOM/`.
- Restart the camera after changing the selected WAD.

The required Canon audio stubs are already present in the documented Magic
Lantern base commit; no Doom-specific core modification is required. An older
Magic Lantern build must be replaced or rebuilt if its symbol file does not
export the audio functions listed in the README.

No WAD files or other copyrighted game assets are included.

## Known limitations

- The selector checks `.wad` plus the `IWAD` header but does not yet fully
  reject non-Doom games.
- PWAD/mod loading is not implemented.
- WAD switching requires a camera restart.
- At most 32 IWADs are listed.
- Multiplayer is not supported.
- When enabled, logs retain only the most recent run.

## Release binary

```text
doom.mo
SHA-256: d7cc379bbd21968ba72bf9359bb03cc1f473904035547229cc217b1f920f56f7
```
