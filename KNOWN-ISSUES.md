# PenguinScreen64 — Known Issues

Honest state of this source release. This is an **early** release: the core
compiles and the VR path works on the developers' hardware, but it has not had
a broad field-testing pass yet.

## Release state

- **Source-only.** No binary packages yet; you build it yourself
  ([QUICKSTART.md](QUICKSTART.md)). Packaged releases with an install kit
  follow once the first field-validation pass completes.
- **The video plugin ships separately** (PenguinScreen64-video). The core's VR
  bridge degrades gracefully when paired with a stock video plugin: you get the
  flat screen, not per-eye depth.
- **Profile coverage starts small.** The shipped catalog covers the launch set
  of tuned games; everything else runs on the universal virtual screen. The
  catalog is plain INI — contributions of measured profiles are the fastest way
  to grow it.

## Behaviour notes

- **Stereo values are not portable between emulators.** This core consumes
  convergence in raw clip-W units (true dual-view geometry). Numbers from other
  VR emulator projects (normalized or DIBR-based) will not transfer.
- **A game can look flat if its profile was tuned for a different region/rev.**
  Profiles key on the ROM's MD5, so a different dump revision simply gets no
  profile — that's expected, not a bug. Copy the section to your dump's MD5 as
  a starting point.
- **Depth comfort varies per player.** `separation` is a comfort knob; lower it
  if near objects are hard to fuse. Profile values are starting points, and
  user global settings win over profile defaults by design.
