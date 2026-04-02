#!/usr/bin/env python3
"""
Generate an 8x16 bitmap font from DejaVu Sans Mono for kernel console use.
Outputs C array data in LSB-first bit ordering (bit 0 = leftmost pixel).
"""

from PIL import Image, ImageDraw, ImageFont
import os, sys

CELL_W, CELL_H = 8, 16
THRESHOLD = 80  # Lower threshold = bolder strokes for better readability at small size

FONT_PATH = "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf"
if not os.path.exists(FONT_PATH):
    FONT_PATH = "/usr/share/fonts/truetype/ubuntu/UbuntuMono-R.ttf"

# Find optimal point size: largest that fits in the cell
best_size = 10
for sz in range(8, 18):
    fnt = ImageFont.truetype(FONT_PATH, sz)
    ascent, descent = fnt.getmetrics()
    # Check that 'M' (widest char) fits horizontally
    bbox = fnt.getbbox('M')
    w = bbox[2] - bbox[0]
    if ascent + descent <= CELL_H and w <= CELL_W:
        best_size = sz

font = ImageFont.truetype(FONT_PATH, best_size)
ascent, descent = font.getmetrics()
total_h = ascent + descent
# Vertical offset to center the font vertically in the cell
y_offset = max(0, (CELL_H - total_h) // 2)

print(f"// Font: {FONT_PATH}", file=sys.stderr)
print(f"// Size: {best_size}pt, ascent={ascent}, descent={descent}, y_offset={y_offset}", file=sys.stderr)

# Preview mode: also output ASCII art of each glyph
preview = "--preview" in sys.argv

def render_char(ch):
    """Render one character, return list of 16 bytes (LSB-first)."""
    img = Image.new('L', (CELL_W, CELL_H), 0)
    draw = ImageDraw.Draw(img)

    # Get character bounding box for horizontal centering
    bbox = font.getbbox(ch)
    char_w = bbox[2] - bbox[0]

    # Center horizontally, use y_offset for vertical positioning
    x_pos = (CELL_W - char_w) / 2.0 - bbox[0]

    draw.text((x_pos, y_offset), ch, font=font, fill=255)

    pix = img.load()
    rows = []
    for r in range(CELL_H):
        byte_val = 0
        for c in range(CELL_W):
            if pix[c, r] > THRESHOLD:
                byte_val |= (1 << c)  # LSB-first: bit 0 = leftmost pixel
        rows.append(byte_val)
    return rows, img

# Generate C array
print("static const uint8_t font_8x16[95][16] = {")
for code in range(32, 127):
    ch = chr(code)
    rows, img = render_char(ch)

    hex_str = ", ".join(f"0x{b:02X}" for b in rows)

    # Escape special chars for comment
    display = ch
    if ch == '\\': display = '\\\\'
    elif ch == '*' and False: display = ch  # fine in comment
    elif ch == '/': display = '/'

    print(f"\t/* ASCII {code} ({display}) */")
    print(f"\t{{{hex_str}}},")

    if preview:
        print(f"  // {ch}:", file=sys.stderr)
        pix = img.load()
        for r in range(CELL_H):
            line = "  // "
            for c in range(CELL_W):
                line += "##" if pix[c, r] > THRESHOLD else ".."
            print(line, file=sys.stderr)
        print(file=sys.stderr)

print("};")
print("// Font generation complete.", file=sys.stderr)
