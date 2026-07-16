Doom 550D
=========

:Author: Bas Lichtjaar <doom@lauris.nl> and contributors
:License: GPL-2.0
:Summary: Experimental Doomgeneric port for the Canon EOS 550D.

Place authorized Doom-family IWAD files in ``ML/DOOM/``. The Games menu lists
up to 32 files with a ``.wad`` extension and an ``IWAD`` header. PWAD level and
mod files are not offered as standalone games. Restart the camera after
changing the selected WAD once Doom has run.

Savegames and settings
----------------------

The module creates ``ML/DOOM``, ``ML/DOOM/SAVES`` and ``ML/DOOM/CONFIG``.
Savegames use a hash of the exact WAD filename so games remain separated::

    ML/DOOM/SAVES/<iwad hash>.D<slot>

Doom menu options, music volume and sound-effect volume are stored in
``ML/DOOM/CONFIG``.

Controls
--------

* Arrows: move and turn
* Zoom out/in: strafe left/right
* SET: fire; confirm in menus
* PLAY: use/open doors and switches
* Rear wheel: previous/next owned weapon
* INFO: automap; back in menus
* MENU: open/close the Doom menu
* Q: confirm the Doom quit dialog
* Front depth-of-field button: run/speed (experimental)

The Canon shutter buttons are deliberately not used for firing.

Audio
-----

MUS music and Doom's 8-bit sound effects are mixed into one 48 kHz mono Canon
ASIF-DMA stream. The installed Magic Lantern core and ``550D_109.sym`` must
export the Canon audio functions required by the module.

This module is only for the Canon EOS 550D with firmware 1.0.9. It remains
experimental; back up the SD card before installation.
