# Contributing to Doom550D

Doom550D is experimental software. Its primary platform is the Canon EOS 550D
(Rebel T2i / Kiss X4), running Canon firmware 1.0.9 and the Magic Lantern
`550D.109` target. Contributions should preserve that platform unless a change
explicitly introduces and documents broader support.

## Building

From a configured Magic Lantern build environment, run:

```sh
make clean
make -j"$(nproc)"
```

## Testing changes

Test changes on the target camera and report the camera model, Magic Lantern
build, Doom550D commit, and IWADs used. At minimum, verify:

- Controls during gameplay, including movement and menu input
- Opening, navigating, and closing menus
- Opening the secret cheat menu with three quick Delete/Trash presses;
  navigating Cheats, Music, and Level; toggling stateful cheats; cycling all
  three IDDT positions; and returning cleanly to gameplay
- Every Screen Size setting, including switching to and from extra unofficial
  fullscreen during gameplay
- Saving and loading games
- IWAD discovery and selection
- Music and sound-effect playback
- Clean shutdown and return to Magic Lantern without stuck input or audio

Attach large logs as files instead of pasting them into an issue or pull
request. Photos or short excerpts may be included where they clarify a result.

## Implementation constraints

- Do not perform SD-card I/O from button callbacks. Defer it to a safe task or
  processing context.
- Do not commit or distribute commercial Doom IWADs or other proprietary game
  assets.
- Freedoom IWADs may be used and shared in accordance with the Freedoom license.

Keep pull requests focused, explain the reason for the change, and document any
known limitations or untested behavior.
