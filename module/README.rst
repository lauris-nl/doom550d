Doom 550D
=========

:Author: Bas Lichtjaar <doom@lauris.nl> and contributors
:License: GPL-2.0
:Summary: Experimental Doomgeneric port for the Canon EOS 550D.

Place authorized Doom-family IWAD files in ``ML/DOOM/``. The Games menu lists
up to 32 alphabetically sorted files with a ``.wad`` extension and an ``IWAD``
header. PWAD level and mod files are not offered as standalone games. Restart
the camera after changing the selected WAD once Doom has run.

Savegames and settings
----------------------

The module creates ``ML/DOOM``, ``ML/DOOM/SAVES`` and ``ML/DOOM/CONFIG``.
Savegames use a hash of the exact WAD filename so games remain separated::

    ML/DOOM/SAVES/<iwad hash>.D<slot>

Selecting a slot with SET saves directly. Doom menu options, music volume and
sound-effect volume are stored in ``ML/DOOM/CONFIG``.

Controls
--------

* Arrows: move and turn
* Hold zoom in (+): run
* Hold zoom out (-) plus left/right: strafe left/right
* SET: fire; confirm in menus
* PLAY: use/open doors and switches
* Rear wheel: previous/next owned weapon
* DISP. (called INFO internally by Magic Lantern): automap; back in menus
* MENU: open/close the Doom menu
* Delete/Trash: Escape/back
* Q: confirm the Doom quit dialog

ISO, depth-of-field and shutter controls are deliberately not used because
Canon can process them below the module input layer.

Audio
-----

MUS music is rendered by a 24-voice low-CPU synthesizer at 24 kHz and mixed
with Doom's 8-bit effects in one 48 kHz mono Canon ASIF-DMA stream. Instrument
families use several inexpensive waveforms, envelopes and percussion. The
result has an AdLib-like retro character but is not cycle-accurate OPL2.

The installed Magic Lantern core and ``550D_109.sym`` must export the Canon
audio functions required by the module. Audio render timing and missed
deadlines are written to ``ML/LOGS/DOOM550D.LOG`` on shutdown.

This module is only for the Canon EOS 550D with firmware 1.0.9. It remains
experimental; back up the SD card before installation.
