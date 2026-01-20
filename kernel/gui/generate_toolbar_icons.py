#!/usr/bin/env python3
"""
Convert toolbar PNG icons to C arrays for vib-OS kernel.
Icons should be 24x24 RGBA PNGs in the same directory.
"""

import os
import sys

try:
    from PIL import Image
except ImportError:
    print("PIL not found. Installing...")
    os.system("pip3 install pillow")
    from PIL import Image

ICON_SIZE = 24

# Icon names for toolbar
ICONS = [
    ("icon_prev", "prev.png"),
    ("icon_next", "next.png"),
    ("icon_rotate_cw", "rotate_cw.png"),
    ("icon_rotate_ccw", "rotate_ccw.png"),
    ("icon_zoom_in", "zoom_in.png"),
    ("icon_zoom_out", "zoom_out.png"),
    ("icon_fit", "fit.png"),
    ("icon_fullscreen", "fullscreen.png"),
]

def render_chevron_left(size):
    """Generate left chevron icon programmatically"""
    from PIL import ImageDraw
    img = Image.new('RGBA', (size, size), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)
    # Draw anti-aliased chevron
    cx, cy = size // 2, size // 2
    points = [(cx + 4, cy - 7), (cx - 4, cy), (cx + 4, cy + 7)]
    draw.line(points, fill=(255, 255, 255, 255), width=3)
    return img

def render_chevron_right(size):
    """Generate right chevron icon programmatically"""
    from PIL import ImageDraw
    img = Image.new('RGBA', (size, size), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)
    cx, cy = size // 2, size // 2
    points = [(cx - 4, cy - 7), (cx + 4, cy), (cx - 4, cy + 7)]
    draw.line(points, fill=(255, 255, 255, 255), width=3)
    return img

def render_rotate_cw(size):
    """Generate clockwise rotation icon"""
    from PIL import ImageDraw
    img = Image.new('RGBA', (size, size), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)
    # Draw arc
    draw.arc([4, 4, size-4, size-4], 45, 315, fill=(255, 255, 255, 255), width=2)
    # Arrow head at end of arc
    draw.polygon([(size-6, 6), (size-3, 10), (size-10, 8)], fill=(255, 255, 255, 255))
    return img

def render_rotate_ccw(size):
    """Generate counter-clockwise rotation icon"""
    from PIL import ImageDraw
    img = Image.new('RGBA', (size, size), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)
    # Draw arc
    draw.arc([4, 4, size-4, size-4], 225, 495, fill=(255, 255, 255, 255), width=2)
    # Arrow head
    draw.polygon([(6, 6), (10, 3), (8, 10)], fill=(255, 255, 255, 255))
    return img

def render_zoom_in(size):
    """Generate zoom in (+) icon"""
    from PIL import ImageDraw
    img = Image.new('RGBA', (size, size), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)
    cx, cy = size // 2, size // 2
    # Plus sign
    draw.line([(cx - 6, cy), (cx + 6, cy)], fill=(255, 255, 255, 255), width=3)
    draw.line([(cx, cy - 6), (cx, cy + 6)], fill=(255, 255, 255, 255), width=3)
    return img

def render_zoom_out(size):
    """Generate zoom out (-) icon"""
    from PIL import ImageDraw
    img = Image.new('RGBA', (size, size), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)
    cx, cy = size // 2, size // 2
    # Minus sign
    draw.line([(cx - 6, cy), (cx + 6, cy)], fill=(255, 255, 255, 255), width=3)
    return img

def render_fit(size):
    """Generate fit-to-window icon (box with corners)"""
    from PIL import ImageDraw
    img = Image.new('RGBA', (size, size), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)
    # Box outline
    draw.rectangle([4, 4, size-5, size-5], outline=(255, 255, 255, 255), width=2)
    # Diagonal arrows pointing inward
    draw.line([(4, 4), (10, 10)], fill=(255, 255, 255, 255), width=2)
    draw.line([(size-5, size-5), (size-11, size-11)], fill=(255, 255, 255, 255), width=2)
    return img

def render_fullscreen(size):
    """Generate fullscreen icon (4 corners)"""
    from PIL import ImageDraw
    img = Image.new('RGBA', (size, size), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)
    # Top-left corner
    draw.line([(2, 8), (2, 2), (8, 2)], fill=(255, 255, 255, 255), width=2)
    # Top-right corner
    draw.line([(size-9, 2), (size-3, 2), (size-3, 8)], fill=(255, 255, 255, 255), width=2)
    # Bottom-left corner
    draw.line([(2, size-9), (2, size-3), (8, size-3)], fill=(255, 255, 255, 255), width=2)
    # Bottom-right corner
    draw.line([(size-9, size-3), (size-3, size-3), (size-3, size-9)], fill=(255, 255, 255, 255), width=2)
    return img

def image_to_c_array(img, name):
    """Convert PIL image to C array of RGBA values"""
    img = img.convert('RGBA')
    pixels = list(img.getdata())
    
    lines = [f"/* {name} - {img.width}x{img.height} RGBA icon */"]
    lines.append(f"static const uint32_t {name}[{img.width * img.height}] = {{")
    
    for y in range(img.height):
        row = []
        for x in range(img.width):
            r, g, b, a = pixels[y * img.width + x]
            # Pack as ARGB: 0xAARRGGBB
            pixel = (a << 24) | (r << 16) | (g << 8) | b
            row.append(f"0x{pixel:08X}")
        lines.append("    " + ", ".join(row) + ",")
    
    lines.append("};")
    return "\n".join(lines)

def main():
    size = ICON_SIZE
    
    # Generate icons programmatically
    icons = [
        ("toolbar_icon_prev", render_chevron_left(size)),
        ("toolbar_icon_next", render_chevron_right(size)),
        ("toolbar_icon_rotate_cw", render_rotate_cw(size)),
        ("toolbar_icon_rotate_ccw", render_rotate_ccw(size)),
        ("toolbar_icon_zoom_in", render_zoom_in(size)),
        ("toolbar_icon_zoom_out", render_zoom_out(size)),
        ("toolbar_icon_fit", render_fit(size)),
        ("toolbar_icon_fullscreen", render_fullscreen(size)),
    ]
    
    # Generate C header
    output = []
    output.append("/*")
    output.append(" * Toolbar Icons for vib-OS Image Viewer")
    output.append(" * Auto-generated 24x24 RGBA icons")
    output.append(" */")
    output.append("")
    output.append("#ifndef TOOLBAR_ICONS_H")
    output.append("#define TOOLBAR_ICONS_H")
    output.append("")
    output.append("#include \"types.h\"")
    output.append("")
    output.append(f"#define TOOLBAR_ICON_SIZE {size}")
    output.append("")
    
    for name, img in icons:
        output.append(image_to_c_array(img, name))
        output.append("")
    
    # Array of pointers for easy access
    output.append("/* Icon array for toolbar */")
    output.append("static const uint32_t* toolbar_icons[] = {")
    for name, _ in icons:
        output.append(f"    {name},")
    output.append("};")
    output.append("")
    output.append("#define TOOLBAR_ICON_PREV 0")
    output.append("#define TOOLBAR_ICON_NEXT 1")
    output.append("#define TOOLBAR_ICON_ROTATE_CW 2")
    output.append("#define TOOLBAR_ICON_ROTATE_CCW 3")
    output.append("#define TOOLBAR_ICON_ZOOM_IN 4")
    output.append("#define TOOLBAR_ICON_ZOOM_OUT 5")
    output.append("#define TOOLBAR_ICON_FIT 6")
    output.append("#define TOOLBAR_ICON_FULLSCREEN 7")
    output.append("")
    output.append("#endif /* TOOLBAR_ICONS_H */")
    
    print("\n".join(output))

if __name__ == "__main__":
    main()
