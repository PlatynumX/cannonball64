# Cannonball64 r2 — full-core port

This revision changes strategy from a bootstrap/demo to compiling the complete
Cannonball Libretro core directly into a libdragon N64 ROM.

It is a **porting build**, not a claim that the complete game is already
verified on hardware. The first CI build is expected to expose any remaining
desktop/libretro-common assumptions; those should be patched at the platform
boundary rather than by rewriting Cannonball gameplay.

## Architecture

Cannonball core
    -> Libretro callbacks
        -> Cannonball64 N64 frontend
            -> libdragon

Implemented N64 frontend pieces:

- 320x240 16-bit VI
- RGB565 -> N64 RGBA5551 video conversion
- 320x224 native Cannonball presentation
- libdragon audio output
- N64 analog/digital controls
- Z gear toggle
- Rumble Pak forwarding
- SD filesystem paths
- high-score save path
- USB/emulator logging
- GitHub Actions full-core build

See `docs/ARCHITECTURE.md`.

## Build

The repo intentionally does not vendor Cannonball.

GitHub Actions automatically fetches:
    https://github.com/libretro/cannonball

For a local libdragon build:

    chmod +x scripts/fetch_core.sh
    ./scripts/fetch_core.sh
    make -j2

Output:
    cannonball64-r2.z64

## ROM setup

Cannonball requires the original OutRun Revision B arcade ROM files.

Target MAME set:
    outrun
    Out Run (sitdown/upright, Rev B)

They must be uncompressed on the flashcart SD card:

    /cannonball/epr-10380b.133
    /cannonball/epr-10382b.118
    ...
    /cannonball/opr-10188.71

See `ROM_REQUIREMENTS.txt`.

Validate an extracted set:

    python3 scripts/check_roms.py /path/to/outrun

Or prepare an SD folder:

    ./scripts/prepare_sd.sh /path/to/extracted/outrun

## N64 controls

- Analog stick: steering
- A: accelerator
- B: brake
- Z: low/high gear toggle
- Start: start
- C-Down: coin
- L: viewpoint
- R: Cannonball menu
- D-pad: menu/navigation

## First CI target

The next useful result is the compiler output from this full-core build.

If it links immediately, excellent: put the verified ROM set on SD and boot
the generated ROM.

If it fails, do not retreat to a fake/demo renderer. Patch each portability
error until this exact full core links and boots.
