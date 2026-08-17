# Cannonball64 architecture

## Why the Libretro core is the port base

The Libretro Cannonball fork already turns Cannonball into a frontend-neutral
core with callbacks for:

- one rendered frame
- stereo PCM audio
- digital/analog input
- filesystem/system directory
- logging
- rumble
- timing and geometry

That is almost exactly the interface we need on Nintendo 64.

Instead of translating/reimplementing the OutRun engine, Cannonball64 links
the core directly into the ROM executable and acts as a very small Libretro
frontend implemented with libdragon.

## Current N64 bridge

### Video

Core:
    RGB565, native 320x224 with N64-safe settings

N64:
    320x240 RGBA5551 triple-buffered VI framebuffer

The frontend converts each pixel:
    RGB565 -> RGBA5551

The native 224-line image is vertically centered, yielding 8 black lines at
the top and bottom.

This is intentionally a correctness-first software path. Once the whole game
runs, profile it before replacing anything with RDP/RSP acceleration.

### Audio

Cannonball's own YM2151 + Sega PCM emulation stays intact.

The Libretro core emits one frame of signed 16-bit interleaved stereo audio.
The frontend sends it into libdragon's AI subsystem with audio_push().

### Input

N64:
    Analog stick X  -> steering
    A               -> accelerator
    B               -> brake
    Z               -> toggle low/high gear
    Start           -> Start
    C-Down          -> coin
    L               -> viewpoint
    R               -> Cannonball menu
    D-pad           -> frontend/menu navigation

The N64 frontend presents Cannonball with its normal Libretro controls; the
engine does not know it is running on N64.

### Rumble

Libretro rumble requests are forwarded to libdragon's Joypad rumble API.

### Files

The frontend reports:
    system directory = sd://

Cannonball therefore resolves its game directory to:
    sd://cannonball/

This is why the OutRun ROM files go directly in `/cannonball/` on the SD card.

### Save data

High score files use:
    sd://cannonball/

This avoids introducing a new N64-specific serialization format until the
core is fully running.

## What is expected to fail first

The port deliberately compiles the full core rather than manually stubbing
large sections. The first GitHub Actions build may expose desktop assumptions
inside libretro-common or the current Cannonball source.

Those compiler errors are useful: each one identifies a concrete portability
dependency to replace. The rule for follow-up revisions is:

1. do not rewrite engine logic,
2. patch platform assumptions at their boundary,
3. keep behavior identical,
4. retest the whole core after every patch.

## Performance order

Do not optimize before we have a real frame.

Once booting:
1. measure frame CPU time,
2. measure RGB conversion cost,
3. measure road/sprite/tile renderer cost,
4. measure YM2151/PCM cost,
5. move only proven bottlenecks to RDP/RSP or specialized paths.

Expansion Pak is the initial target while bringing up the complete core.
