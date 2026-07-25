# PenguinScreen64 — Building from source

This is a **source release** of the core component. It builds exactly like
upstream Mupen64Plus-core, because it is one — plus a VR presentation path.

## What you need

- A Linux machine (the VR path targets Linux + OpenXR; the flat build works
  everywhere upstream does).
- The usual Mupen64Plus toolchain: `gcc`/`clang`, `make`, SDL2, libpng, zlib,
  freetype. For the VR path: a Vulkan-capable GPU and an OpenXR runtime
  (a wireless streaming runtime such as WiVRn, or any conformant runtime).
- **Your own game dumps.** Nothing is included or downloaded, ever.

## Build (same as upstream)

```
cd projects/unix
make all
```

The result is the core library (`libmupen64plus.so.2`). It loads into any
Mupen64Plus frontend alongside the standard plugin set; the VR render path
pairs with the PenguinScreen64-video-plugin.

## VR profiles

Per-game VR behaviour lives in `data/vr-profiles.ini` — one `[<ROM MD5>]`
section per game (the same MD5 key `mupen64plus.ini` uses). A profile is plain
data: stereo `separation` / `convergence`, head-look settings, screen geometry.
Unknown keys are ignored; a bad value is skipped with a warning, never a
crash. Copy an existing section as a template for a new game.

Games without a profile still get the universal head-tracked virtual screen;
profiles are what enable per-game stereo depth and head-look.

## Runtime notes (VR link)

If your OpenXR runtime streams to a standalone headset over the network
(WiVRn-class), note that some immutable/locked-down distros block mDNS service
announcements at the OS level — the server then cannot announce itself and the
headset won't auto-discover the PC. Connecting **by IP** always works; check
your runtime's documentation for its connect-by-address flow.

## Known limitations

See [KNOWN-ISSUES.md](KNOWN-ISSUES.md) — honest state, updated per release.
