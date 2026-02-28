#!/usr/bin/env python3
"""
Generate native bitmap fonts at multiple resolutions for TomahawkOS console.
Produces three font tables:
  font_8x16   – for 1024x768 and below
  font_16x32  – for 1080p/1440p
  font_24x48  – for 4K (3840x2160)

Each is rasterized natively from Liberation Mono (CMD-like monospace font)
at the target pixel size, so there is NO upscaling.

Bit ordering: bit 0 = leftmost pixel (LSB-first).
"""

from PIL import Image, ImageDraw, ImageFont
import os, sys

# Font search order: Liberation Mono (closest to Consolas / CMD), then fallbacks
FONT_CANDIDATES = [
    "/usr/share/fonts/truetype/liberation/LiberationMono-Regular.ttf",
    "/usr/share/fonts/truetype/liberation/LiberationMono-Bold.ttf",
    "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
    "/usr/share/fonts/truetype/ubuntu/UbuntuMono-R.ttf",
]

def find_font():
    for p in FONT_CANDIDATES:
        if os.path.exists(p):
            return p
    print("ERROR: No suitable monospace font found!", file=sys.stderr)
    sys.exit(1)

FONT_PATH = find_font()
print(f"// Using font: {FONT_PATH}", file=sys.stderr)

# ──────────────────────────────────────────────────────────────

def best_point_size(cell_w, cell_h, bold_threshold):
    """Find the largest point size whose metrics fit inside cell_w x cell_h."""
    best = 8
    for sz in range(6, cell_h + 4):
        fnt = ImageFont.truetype(FONT_PATH, sz)
        ascent, descent = fnt.getmetrics()
        bbox = fnt.getbbox('M')
        w = bbox[2] - bbox[0]
        if ascent + descent <= cell_h and w <= cell_w:
            best = sz
    return best

def render_char(ch, cell_w, cell_h, font, y_offset):
    """Render one character into a cell_w x cell_h bitmap.
    Returns a list of row integers (LSB-first) with cell_w bits each."""
    img = Image.new('L', (cell_w, cell_h), 0)
    draw = ImageDraw.Draw(img)

    bbox = font.getbbox(ch)
    char_w = bbox[2] - bbox[0]
    x_pos = (cell_w - char_w) / 2.0 - bbox[0]
    draw.text((x_pos, y_offset), ch, font=font, fill=255)

    pix = img.load()
    # Adaptive threshold: lower for small cells (bolder strokes)
    threshold = 60 if cell_w <= 8 else 80 if cell_w <= 16 else 100

    rows = []
    for r in range(cell_h):
        val = 0
        for c in range(cell_w):
            if pix[c, r] > threshold:
                val |= (1 << c)   # bit 0 = leftmost pixel
        rows.append(val)
    return rows

def generate_font(cell_w, cell_h, typename, arrname):
    """Generate one font table and return the C source as a string."""
    pt = best_point_size(cell_w, cell_h, 80)
    font = ImageFont.truetype(FONT_PATH, pt)
    ascent, descent = font.getmetrics()
    total_h = ascent + descent
    y_offset = max(0, (cell_h - total_h) // 2)

    print(f"// {arrname}: {cell_w}x{cell_h}  pt={pt}  ascent={ascent}  descent={descent}  y_off={y_offset}", file=sys.stderr)

    # Determine hex format width based on cell_w
    if cell_w <= 8:
        hex_fmt = "0x{:02X}"
    elif cell_w <= 16:
        hex_fmt = "0x{:04X}"
    else:
        hex_fmt = "0x{:08X}"

    lines = []
    lines.append(f"static const {typename} {arrname}[95][{cell_h}] = {{")

    for code in range(32, 127):
        ch = chr(code)
        rows = render_char(ch, cell_w, cell_h, font, y_offset)
        hex_str = ", ".join(hex_fmt.format(v) for v in rows)
        display = ch
        if ch == '\\': display = '\\\\'
        lines.append(f"\t/* {code} ({display}) */")
        lines.append(f"\t{{{hex_str}}},")

    lines.append("};")
    return "\n".join(lines)

# ──────────────────────────────────────────────────────────────
# Generate the header

header = []
header.append("#ifndef FONT_8X16_H")
header.append("#define FONT_8X16_H")
header.append("")
header.append("#include <stdint.h>")
header.append("")
header.append("/**")
header.append(" * @file font_8x16.h")
header.append(" * @brief Multi-resolution bitmap fonts rasterised from Liberation Mono")
header.append(" *")
header.append(" * Three native font tables so each resolution gets crisp glyphs")
header.append(" * without nearest-neighbour upscaling.")
header.append(" *   font_8x16  – 1024x768 and below")
header.append(" *   font_16x32 – 1080p / 1440p")
header.append(" *   font_24x48 – 4K (3840x2160)")
header.append(" *")
header.append(" * Bit ordering: bit 0 = leftmost pixel.")
header.append(" */")
header.append("")

# Generate all three sizes
header.append("/* ═══════════  8 x 16  (768p and below)  ═══════════ */")
header.append(generate_font(8, 16, "uint8_t", "font_8x16"))
header.append("")
header.append("/* ═══════════  16 x 32  (1080p / 1440p)  ═══════════ */")
header.append(generate_font(16, 32, "uint16_t", "font_16x32"))
header.append("")
header.append("/* ═══════════  24 x 48  (4K)  ═══════════ */")
header.append(generate_font(24, 48, "uint32_t", "font_24x48"))
header.append("")

# ── Draw helpers ──

header.append("""
/* ═══════════  Drawing helpers  ═══════════ */

static inline void draw_char_8x16(volatile uint32_t *fb, uint32_t pitch,
    char c, uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
    if (c < 32 || c > 126) return;
    const uint8_t *bm = font_8x16[c - 32];
    for (int r = 0; r < 16; r++) {
        uint8_t bits = bm[r];
        uint32_t py = y + r;
        if (py >= h) break;
        for (int col = 0; col < 8; col++) {
            if (bits & (1 << col)) {
                uint32_t px = x + col;
                if (px < w) fb[py * pitch + px] = 0xFFFFFFFF;
            }
        }
    }
}

static inline void draw_char_16x32(volatile uint32_t *fb, uint32_t pitch,
    char c, uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
    if (c < 32 || c > 126) return;
    const uint16_t *bm = font_16x32[c - 32];
    for (int r = 0; r < 32; r++) {
        uint16_t bits = bm[r];
        uint32_t py = y + r;
        if (py >= h) break;
        for (int col = 0; col < 16; col++) {
            if (bits & (1 << col)) {
                uint32_t px = x + col;
                if (px < w) fb[py * pitch + px] = 0xFFFFFFFF;
            }
        }
    }
}

static inline void draw_char_24x48(volatile uint32_t *fb, uint32_t pitch,
    char c, uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
    if (c < 32 || c > 126) return;
    const uint32_t *bm = font_24x48[c - 32];
    for (int r = 0; r < 48; r++) {
        uint32_t bits = bm[r];
        uint32_t py = y + r;
        if (py >= h) break;
        for (int col = 0; col < 24; col++) {
            if (bits & (1 << col)) {
                uint32_t px = x + col;
                if (px < w) fb[py * pitch + px] = 0xFFFFFFFF;
            }
        }
    }
}

/* Legacy compatibility wrapper — used by old draw_char_scaled callers */
static inline void draw_char_scaled(volatile uint32_t *fb, uint32_t pitch,
    char c, uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t scale)
{
    if (scale >= 3)
        draw_char_24x48(fb, pitch, c, x, y, w, h);
    else if (scale == 2)
        draw_char_16x32(fb, pitch, c, x, y, w, h);
    else
        draw_char_8x16(fb, pitch, c, x, y, w, h);
}
""")

header.append("#endif /* FONT_8X16_H */")

print("\n".join(header))
