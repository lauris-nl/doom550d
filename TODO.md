# TODO

## Audio quality

- Continue tuning instrument-family waveforms, envelopes, percussion and mix
  balance without increasing camera CPU load significantly.
- Compare the stable low-CPU synth with a future table-driven OPL2 backend.
- Keep music and Doom sound effects in the same low-CPU mixer.
- Verify clipping, channel balance and multi-hour session stability.

## WAD handling

- Reject Heretic, Hexen, Strife, Doom 64 and other incompatible IWADs before
  launch instead of checking only the `IWAD` header.
- Add the Chocolate Doom DEHACKED parser and load Hacx 1.2's embedded
  `DEHACKED` lump before the title loop. Verify its music-name replacements,
  weapons, enemies, all maps, saves and repeated starts. A one-off
  `D_DM2TTL` to `D_HAXTTL` alias is not a complete Hacx compatibility fix.
- Add base-IWAD plus PWAD selection for compatible level packs such as SIGIL
  and the Master Levels.
- Investigate safe WAD switching without a full camera reboot.

## Diagnostics and testing

- Preserve or rotate logs per WAD instead of overwriting the previous run.
- Record loaded game mode/version in failure logs.
- Test canonical Doom, Doom II, TNT, Plutonia and Freedoom IWADs.
- Run the complete Hacx regression test after DEHACKED support is implemented.
- Continue long-duration save/load, quit, repeated-start, palette and control
  regression testing.

## Completed in v0.4.0

- Add the secret in-game menu for classic cheats, available IWAD music, and
  present levels.
- Add live Doom-style state sliders, including all three IDDT automap modes.
- Keep camera callbacks free of SD-card I/O and isolate paused menu input from
  gameplay and the Magic Lantern menu.

## Completed in v0.3.2

- Add the extra unofficial fullscreen Screen Size setting with a 360x240
  gameplay view and exact 2x2 output at 720x480.

## Completed in v0.3.1

- Add a persistent debug logging switch to the Magic Lantern Games menu.
- Default diagnostic file writes to off for normal play.
- Add safe removal of `ML/LOGS/DOOM*.LOG` without touching WADs, saves or
  configuration.

## Completed in v0.3.0

- Add a richer 24-voice, 24 kHz low-CPU music synthesizer.
- Add instrument families, envelopes, percussion, channel volume and pitch
  bend while preserving fluid gameplay.
- Map held zoom in to run and held zoom out plus left/right to strafe.
- Save directly from a selected slot and clear input after a successful save.
- Validate WAD cache ownership after loading saves.
- Block Delete/Trash from opening a hidden Magic Lantern menu during Doom.
- Restore display, palette, input and audio state after repeated runs.

## Completed in v0.2.0-beta.1

- Limit Canon `FIO_ReadFile` operations to safe chunks.
- Select IWADs from the Magic Lantern menu.
- Separate saves per WAD and use Canon-compatible save filenames.
- Create and validate save/config directories automatically.
- Persist menu, sound, music and custom camera-control settings.
- Mix MUS music and Doom sound effects through Canon ASIF audio.
