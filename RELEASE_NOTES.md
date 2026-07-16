# Doom 550D v0.2.0-beta.1

This beta was tested on a Canon EOS 550D with firmware 1.0.9. The installed
`doom.mo` was marked **very good** after physical-camera testing.

## Highlights

- Select up to 32 Doom-family IWADs from the Magic Lantern Games menu.
- Keep savegames strictly separated for every WAD filename.
- Persist Doom menu, music-volume, and sound-effect settings.
- Play MUS music and mix it with Doom sound effects through the camera speaker.
- Restore Magic Lantern input, display, palette, and audio state on exit.

## Important fixes

- Canon file reads are split into chunks accepted by Magic Lantern FIO.
- Save paths use short hashed names and are checked before the game starts.
- Virtual camera-control codes 160-163 now survive saving and reloading, fixing
  SET/fire, PLAY/use, and both strafe controls after a camera restart.
- WAD changes within one camera session are blocked to prevent mixed engine
  state and memory corruption.

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
- Music currently uses a low-CPU square-wave synthesizer.
- WAD switching requires a camera restart.
- Multiplayer is not supported.
- Logs retain only the most recent run.

## Release binary

```text
doom.mo
SHA-256: a8c32cdf85231079f0ee7ad3ccbcb065792483ae9759c911640526c3d77446b8
```
