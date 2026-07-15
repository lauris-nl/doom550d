# Doom 550D

Doom running as a Magic Lantern module on the Canon EOS 550D / Rebel T2i / Kiss X4.

This release builds a Doomgeneric module for Canon EOS 550D firmware 1.0.9.
The module requires a supported Doom IWAD and does not include any WAD files.

## Supported release target

* Canon EOS 550D
* Canon firmware 1.0.9
* Tested with Magic Lantern build 2025-06-20 for 550D.109
* Released module filename: `doom.mo`

## Installation

1. Install and verify Magic Lantern.
2. Copy `doom.mo` to:

    ML/MODULES/doom.mo

3. Create:

    ML/DOOM/

4. Place exactly one supported IWAD in `ML/DOOM/`.
5. Start Doom from the Magic Lantern Games menu.
6. Saves are stored in `ML/DOOM/` as `doomsav0.dsg`, `doomsav1.dsg`, etc.

> Do not include an IWAD in this repository or release package.

## Supported IWAD files

| Filename | Game |
| --- | --- |
| `doom1.wad` | Doom Shareware (one episode) |
| `doom.wad` | Doom / The Ultimate Doom |
| `doom2.wad` | Doom II |
| `freedoom1.wad` | Freedoom: Phase 1 (four episodes) |
| `freedoom2.wad` | Freedoom: Phase 2 (32 levels) |

These filenames must not be changed.

Download links:

* Freedoom: https://freedoom.github.io/download.html
* DOOM + DOOM II on Steam: https://store.steampowered.com/app/2280/DOOM_1993/
* DOOM + DOOM II on GOG: https://www.gog.com/en/game/doom_doom_ii
* Doom Shareware archive: https://www.doomworld.com/idgames/idstuff/doom/doom19s

## Controls

* Arrow keys: move forward/back and turn
* Zoom out: strafe left
* Zoom in: strafe right
* SET: fire or confirm a menu selection
* PLAY: use/open doors
* Rear wheel left/right: previous/next owned weapon
* INFO: automap during gameplay; back in menus
* MENU: open or close the Doom menu
* Q: confirm the Doom quit dialog
* Canon shutter buttons are deliberately not used

## Why this release is only for the 550D

Doom itself is portable, but the current Magic Lantern integration is specific to the 550D.
This build contains:

* raw Canon 550D press and release event codes
* 550D button assignments
* assumptions about the 550D LiveView bitmap display and palette
* task, memory and module behavior tested only on 550D firmware 1.0.9
* testing against the 550D.109 Magic Lantern symbols and build

Do not install this binary on another camera model.

## Porting to another Magic Lantern camera

A port requires:

* a working Magic Lantern build for the exact camera firmware
* the correct camera symbol file
* mapping of raw button press and release events
* verification of framebuffer address, dimensions, pitch, pixel format and palette handling
* verification of task stack size, memory availability and file I/O
* replacement or isolation of the 550D-specific platform code
* tests for start, exit, restart, save, load, menus and every control
* a separately tested release binary for each camera/firmware combination

## Building doom.mo from source

Use the Magic Lantern source tree:
https://github.com/reticulatedpines/magiclantern_simplified

Requirements:

* Git
* GNU Make
* Python 3
* arm-none-eabi-gcc
* ARM newlib headers/libraries

Example:

```sh
git clone --branch dev \
  https://github.com/reticulatedpines/magiclantern_simplified.git

cd magiclantern_simplified

# Place this project directory at:
# modules/doom550d/

make -C modules/doom550d clean
make -C modules/doom550d -j"$(nproc)"
```

Output:

    modules/doom550d/build/doom.mo

Note: `build/` is intentionally excluded from Git.

## Known limitations

* no sound or music
* internally rendered at 320x200 and displayed at 640x400
* only Canon EOS 550D firmware 1.0.9 is supported
* multiplayer/network play is not supported
* only the listed IWAD filenames are part of this release
* experimental software: make a backup of the memory card

## Credits and license

* Doom engine source by id Software
* Doomgeneric
* Chocolate Doom
* Magic Lantern
* Freedoom contributors
* Bas Lichtjaar <doom@lauris.nl> and contributors to this port

This project is released under GPL-2.0. Third-party game assets are not included.
