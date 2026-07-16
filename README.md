# Doom 550D

Doom running as a Magic Lantern module on the Canon EOS 550D / Rebel T2i / Kiss X4.

This repository contains the module source and release documentation. It does
not contain Doom, Freedoom, or any other WAD data.

> Status: beta/experimental. The `v0.2.0-beta.1` build was tested on a Canon
> EOS 550D and marked **very good**, but it is not intended for other cameras.

## Screenshots

![Doom title screen](screenshots/doom_title.jpg)

![Doom running on Canon 550D](screenshots/doom_ingame1.jpg)

![Doom gameplay on Canon 550D](screenshots/doom_ingame2.jpg)

![Doom menu on Canon 550D](screenshots/doom_menu.jpg)

## Features

- selectable Doom-family IWADs from the Magic Lantern Games menu;
- up to 32 IWAD files in `ML/DOOM/`;
- separate save slots for every exact WAD filename;
- persistent Doom menu, sound, and music settings;
- Doom MUS playback mixed with 8-bit sound effects;
- 48 kHz mono output through the camera speaker;
- cleanup of input, display, palette, and audio state when Doom exits;
- Canon 550D press/release controls, including held movement keys.

## Supported release target

- Canon EOS 550D / Rebel T2i / Kiss X4
- Canon firmware 1.0.9
- Magic Lantern 550D.109 core and `550D_109.sym` built from the matching
  `magiclantern_simplified` tree
- released module filename: `doom.mo`

The beta binary was built from Magic Lantern base commit
`8f8fb3e2f97f156a30da62feeadbfc62244b33bc`. Keep `autoexec.bin`,
`550D_109.sym`, and `doom.mo` from compatible builds together. A module built
against different exported symbols may fail to load.

No Doom-specific Magic Lantern core modification is required for audio. The
required Canon audio stubs are already present in the base commit above. The
installed `550D_109.sym` must export these symbols:

```text
PowerAudioOutput
SetAudioVolumeOut
SetNextASIFDACBuffer
SetSamplingRate
StartASIFDMADAC
StopASIFDMADAC
audio_configure
beep_playing
```

If an older Magic Lantern installation does not export all eight names, build
and install a matching `autoexec.bin` and `550D_109.sym` before loading the
module. Building that ML base with GCC 16 additionally requires the known
`src/tskmon.c` compiler-version guard adjustment; it does not change Doom or
the runtime audio implementation.

Do not install this binary on another camera model or firmware version.

## Installation

1. Back up the SD card and verify that Magic Lantern works normally.
2. Verify that `autoexec.bin` and `550D_109.sym` come from a compatible Magic
   Lantern 550D.109 build.
3. Copy the released module to:

   ```text
   ML/MODULES/doom.mo
   ```

4. Create `ML/DOOM/` or let the module create it.
5. Copy one or more legally obtained Doom-compatible IWAD files into
   `ML/DOOM/`.
6. In the Magic Lantern Games menu, select **Doom > WAD**.
7. Restart the camera after changing the selected WAD, then start Doom.

The module creates and manages `ML/DOOM/SAVES` and `ML/DOOM/CONFIG`.

> WAD files are copyrighted game data. They are not included in this
> repository or its releases.

## WAD compatibility

The selector accepts regular `.wad` files whose first four bytes are `IWAD`.
PWAD level and mod files are not offered as standalone games.

Expected Doom-family IWAD names include:

| Filename | Game |
| --- | --- |
| `doom1.wad` | Doom Shareware |
| `doom.wad` | Doom / The Ultimate Doom |
| `doom2.wad` | Doom II |
| `tnt.wad` | Final Doom: TNT Evilution |
| `plutonia.wad` | Final Doom: Plutonia Experiment |
| `freedoom1.wad` | Freedoom: Phase 1 |
| `freedoom2.wad` | Freedoom: Phase 2 |
| `freedm.wad` | FreeDM; primarily useful for multiplayer |
| `hacx.wad` | Hacx 1.2 |

The current selector checks the WAD header, not the complete game format.
Heretic, Hexen, Strife, Doom 64, PK3 files, and source-port-specific IWADs are
not supported even if a file happens to use an `IWAD` header.

Download links for freely distributable or commercially available data:

- Freedoom: <https://freedoom.github.io/download.html>
- Doom + Doom II on Steam: <https://store.steampowered.com/app/2280/DOOM_1993/>
- Doom + Doom II on GOG: <https://www.gog.com/en/game/doom_doom_ii>
- Doom Shareware archive: <https://www.doomworld.com/idgames/idstuff/doom/doom19s>

## Controls

| Camera control | Doom action |
| --- | --- |
| Arrow keys | Move forward/back and turn |
| Zoom out/in | Strafe left/right |
| SET | Fire; confirm in menus |
| PLAY | Use/open doors and switches |
| Rear wheel | Previous/next owned weapon |
| INFO | Automap; back while in menus |
| MENU | Open/close the Doom menu |
| Q | Confirm the Doom quit dialog |
| Front depth-of-field button | Run/speed; experimental |

The Canon shutter buttons are deliberately consumed without firing because
they otherwise start Canon camera actions.

## Saves and settings

Savegames are separated by a hash of the exact selected WAD filename:

```text
ML/DOOM/SAVES/<iwad-hash>.D<slot>
```

Doom menu settings, music volume, and sound-effect volume are stored in:

```text
ML/DOOM/CONFIG/
```

The selected WAD is stored in the Magic Lantern module configuration.

## Audio

The module reads MUS lumps directly from the selected IWAD. Its lightweight
integer square-wave synthesizer mixes music with Doom's 8-bit samples into one
48 kHz mono ASIF-DMA stream. Music and sound-effect levels are controlled from
the normal Doom sound menu.

The synthesizer is intentionally inexpensive but does not yet sound like an
OPL2/AdLib implementation. Audio quality is a planned improvement.

## Building `doom.mo`

Use the Magic Lantern source tree:
<https://github.com/reticulatedpines/magiclantern_simplified>

Requirements:

- Git
- GNU Make
- Python 3
- `arm-none-eabi-gcc`
- ARM newlib headers and libraries

Place the `module/` directory from this repository at
`modules/doom550d/` in the Magic Lantern tree, then run:

```sh
make -C modules/doom550d -j4
```

Output:

```text
modules/doom550d/build/doom.mo
```

Every explicit module build also creates a source archive under
`modules/doom550d/source-backups/`. Build products and source backups are
ignored by Git.

## Known limitations

- only Canon EOS 550D firmware 1.0.9 is supported;
- changing the WAD after Doom has run requires a camera restart;
- at most 32 IWADs are listed;
- incompatible non-Doom IWADs are not yet rejected reliably;
- PWAD/mod selection is not implemented;
- music uses a basic square-wave synthesizer;
- multiplayer/network play is not supported;
- logs currently retain only the most recent run;
- this remains experimental software: keep SD-card backups.

## Credits and license

- Doom engine source by id Software
- Doomgeneric
- Chocolate Doom
- Magic Lantern
- Freedoom contributors
- Bas Lichtjaar <doom@lauris.nl> and contributors to this port

This project is released under GPL-2.0. Third-party game assets are not
included.
