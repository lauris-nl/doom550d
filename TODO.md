# TODO

## Audio quality

- Replace or complement the square-wave music backend with a lightweight
  integer OPL2/GENMIDI-style synthesizer.
- Keep music and Doom sound effects in the same low-CPU mixer.
- Verify clipping, channel balance, and long-session stability.

## WAD handling

- Reject Heretic, Hexen, Strife, Doom 64, and other incompatible IWADs before
  launch instead of checking only the `IWAD` header.
- Sort all candidates before applying the 32-file display limit.
- Add basis-IWAD plus PWAD selection for compatible level packs such as SIGIL
  and the Master Levels.
- Investigate safe WAD switching without a full camera reboot.

## Diagnostics and testing

- Preserve or rotate logs per WAD instead of overwriting the previous run.
- Record loaded game mode/version and input events in failure logs.
- Test canonical Doom, Doom II, TNT, Plutonia, Freedoom, and Hacx IWADs.
- Test save/load, quit wipes, repeated starts, palette restoration, and every
  camera control for each supported release.
- Verify the front depth-of-field run/speed button on physical hardware.

## Completed in v0.2.0-beta.1

- Limit Canon `FIO_ReadFile` operations to safe chunks.
- Select IWADs from the Magic Lantern menu.
- Separate saves per WAD and use Canon-compatible save filenames.
- Create and validate save/config directories automatically.
- Persist menu, sound, music, and custom camera-control settings.
- Mix MUS music and Doom sound effects through Canon ASIF audio.
- Restore display, palette, input, and audio state when Doom exits.
