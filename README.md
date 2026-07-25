# PenguinScreen64

**Nintendo 64 emulation on a virtual screen in VR, with real stereoscopic
depth.** A fork of the [Mupen64Plus](https://mupen64plus.org/) core, extended
with an OpenXR presentation path: every game plays on a head-tracked virtual
screen, and supported games render with genuine per-eye geometric depth driven
by a per-game profile database.

> **Status: early source release.** This is the core component; it pairs with
> the PenguinScreen64-video plugin (the per-eye render path). Binary releases,
> an install kit, and setup docs arrive with the first packaged build — what's
> here today is the source, the VR profile database, and the build
> instructions in [QUICKSTART.md](QUICKSTART.md).

## What it does

- **Virtual screen for every game** — the emulator presents to a curved,
  head-tracked screen inside your headset over OpenXR.
- **Stereoscopic depth for profiled games** — per-game profiles
  (`data/vr-profiles.ini`) enable true dual-view geometric stereo with tuned
  convergence and separation. Profiles are plain INI data you can edit and
  extend — one `[ROM-MD5]` section per game, no code required.
- **Head-look on supported titles** — headset rotation composed into the
  game's camera on games profiled for it.

## What it is not

This project builds on well-established techniques — OpenXR presentation,
geometric stereo rendering, per-game camera work pioneered across the flat2VR
community — executed for the N64 library with a per-game tuning catalog. The
emulation itself is Mupen64Plus; all credit for N64 emulation belongs to that
project and its contributors.

## License

GPL-2.0-or-later, inherited from Mupen64Plus. See [LICENSES](LICENSES).
Game ROMs are not included and never will be: dump your own cartridges.

## Credits

- The [Mupen64Plus](https://github.com/mupen64plus) team — the emulator.
- The GLideN64 project — the video plugin this fork's render path builds on.
