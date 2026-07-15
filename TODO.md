# TODO

## Fix Magic Lantern `FIO_ReadFile` assertion

Doom may exit with this Magic Lantern assertion:

```text
ML ASSERT:
count <= 8192
at ../../src/fio-ml.c:641 (FIO_ReadFile)
fixme: please use fio_malloc (in doom_task)


## Controls

- Fix the front depth-of-field preview button so it correctly triggers running/speed.

## Game selection

- Add a configuration menu in the Magic Lantern Modules menu, similar to Arkanoid.
- Allow selecting which IWAD/game to launch:
  - Auto
  - Doom 1
  - Doom 2
  - Freedoom 1
  - Freedoom 2

## Documentation / screenshots

- Add screenshots of Doom running on the Canon 550D to the repository.
- Exclude the Arkanoid screenshot from the Doom documentation.


Required work
Limit every FIO_ReadFile request to a maximum of 8192 bytes.
Use a buffer allocated with fio_malloc for direct card reads.
Check doom_ml_compat.c for reads using larger blocks.
Handle short reads and read errors correctly.
Confirm that allocated FIO buffers are always freed.