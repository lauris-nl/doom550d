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
- Add base-IWAD plus PWAD selection for compatible level packs such as SIGIL
  and the Master Levels.
- Investigate safe WAD switching without a full camera reboot.

## Diagnostics and testing

- Preserve or rotate logs per WAD instead of overwriting the previous run.
- Record loaded game mode/version in failure logs.
- Test canonical Doom, Doom II, TNT, Plutonia, Freedoom and Hacx IWADs.
- Continue long-duration save/load, quit, repeated-start, palette and control
  regression testing.

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
