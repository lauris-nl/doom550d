# Contributing to Doom550D

Thank you for your interest in Doom550D.

Doom550D is an experimental Doom port for the Canon EOS 550D running Magic Lantern. Contributions, testing reports and documentation improvements are welcome.

## Before contributing

Please open an issue before making large changes. This helps prevent duplicated work and allows technical details to be discussed first.

Small bug fixes, documentation corrections and narrowly scoped improvements may be submitted directly as a pull request.

## Supported target

The primary supported platform is:

- Canon EOS 550D / Rebel T2i / Kiss X4
- Canon firmware 1.0.9
- Magic Lantern target `550D.109`

Changes must not break the existing 550D build unless the pull request clearly introduces support for another camera as a separate target.

## Building

Doom550D is normally built inside a Magic Lantern source tree:

```text
magiclantern_simplified/modules/doom550d/
