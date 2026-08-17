#!/usr/bin/env python3
from pathlib import Path
import sys

if len(sys.argv) != 2:
    raise SystemExit("usage: patch_core_rdp.py <vendor/cannonball>")

root = Path(sys.argv[1])
video_c = root / "src/main/video.c"

if not video_c.is_file():
    raise SystemExit(f"ERROR: missing {video_c}")

s = video_c.read_text()

# ---------------------------------------------------------------------------
# 1. Produce the N64's native RGBA5551 format directly in Cannonball.
#    This deletes the N64 frontend's old RGB565 -> RGBA5551 pixel loop.
# ---------------------------------------------------------------------------
s = s.replace("#define Bshift 0", "#define Bshift 1", 1)

old_rgb = "#define CURRENT_RGB() ((r << Rshift) | (g << Gshift) | (b << Bshift))"
new_rgb = "#define CURRENT_RGB() ((r << Rshift) | (g << Gshift) | (b << Bshift) | 1)"

if old_rgb in s:
    s = s.replace(old_rgb, new_rgb, 1)
elif new_rgb not in s:
    raise SystemExit("ERROR: CURRENT_RGB macro not found")

# ---------------------------------------------------------------------------
# 2. The text/HUD layer is the final System 16 layer. Skip its software
#    rasterizer so the N64 frontend can draw it with CI4+TLUT on the RDP.
# ---------------------------------------------------------------------------
old_text = "        hwtiles_render_text_layer(self->tile_layer, self->pixels, 1);\n"
new_text = """        /* Cannonball64: text/HUD is rendered by the N64 RDP frontend. */
#ifdef CANNONBALL64
        (void)self;
#else
        hwtiles_render_text_layer(self->tile_layer, self->pixels, 1);
#endif
"""

if old_text in s:
    s = s.replace(old_text, new_text, 1)
elif "text/HUD is rendered by the N64 RDP frontend" not in s:
    raise SystemExit("ERROR: text-layer call not found")

# ---------------------------------------------------------------------------
# 3. Avoid a MIPS divide/modulo for every pixel when the index is already in
#    the valid 3-bank palette span (normal case).
# ---------------------------------------------------------------------------
old_convert = """    { int i; for (i = 0;
         i < config.s16_width * config.s16_height;
         i++)
        output[i] = (uint16_t)
            self->rgb[output[i] % (S16_PALETTE_ENTRIES * 3)]; }
"""

new_convert = """    {
        int i;
        const uint16_t palette_span = (S16_PALETTE_ENTRIES * 3);

        for (i = 0; i < config.s16_width * config.s16_height; i++)
        {
            uint16_t idx = output[i];
            if (idx >= palette_span)
                idx %= palette_span;
            output[i] = (uint16_t)self->rgb[idx];
        }
    }
"""

if old_convert in s:
    s = s.replace(old_convert, new_convert, 1)
elif "const uint16_t palette_span = (S16_PALETTE_ENTRIES * 3);" not in s:
    raise SystemExit("ERROR: final palette conversion loop not found")

video_c.write_text(s)

print("Cannonball RDP core patch applied:")
print("  native RGBA5551 output")
print("  software text/HUD disabled")
print("  normal-case per-pixel modulo removed")
