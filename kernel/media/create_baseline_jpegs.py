#!/usr/bin/env python3
"""
Create simple baseline JPEG images that work with picojpeg decoder.
Requirements:
- Baseline sequential DCT
- No EXIF data
- 4:2:0 or 4:4:4 chroma subsampling
- Standard Huffman tables
"""
from PIL import Image
import os

def create_gradient_image(width, height, color1, color2, direction='horizontal'):
    """Create a simple gradient image"""
    img = Image.new('RGB', (width, height))
    pixels = img.load()
    
    for y in range(height):
        for x in range(width):
            if direction == 'horizontal':
                ratio = x / width
            else:
                ratio = y / height
            
            r = int(color1[0] + (color2[0] - color1[0]) * ratio)
            g = int(color1[1] + (color2[1] - color1[1]) * ratio)
            b = int(color1[2] + (color2[2] - color1[2]) * ratio)
            pixels[x, y] = (r, g, b)
    
    return img

def create_pattern_image(width, height, colors):
    """Create a simple pattern image"""
    img = Image.new('RGB', (width, height))
    pixels = img.load()
    
    for y in range(height):
        for x in range(width):
            # Create a simple checkerboard-like pattern
            idx = ((x // 40) + (y // 40)) % len(colors)
            pixels[x, y] = colors[idx]
    
    return img

def save_baseline_jpeg(img, filename):
    """Save image as baseline JPEG (compatible with picojpeg)"""
    # Convert to RGB if needed
    if img.mode != 'RGB':
        img = img.convert('RGB')
    
    # Save with settings that ensure baseline JPEG
    img.save(filename, 'JPEG', 
             quality=80,
             optimize=False,      # Don't optimize - keep simple
             progressive=False,   # Baseline, not progressive
             subsampling=0)       # 4:4:4 subsampling
    
    print(f"Created {filename} ({os.path.getsize(filename)} bytes)")

# Create output directory
os.makedirs("bootstrap_images", exist_ok=True)

# Create landscape image (blue sky gradient)
print("Creating landscape.jpg...")
img = create_gradient_image(320, 200, (135, 206, 235), (25, 25, 112), 'vertical')
save_baseline_jpeg(img, "bootstrap_images/landscape.jpg")

# Create portrait image (sunset colors)
print("Creating portrait.jpg...")
img = create_gradient_image(200, 320, (255, 183, 77), (128, 0, 128), 'vertical')
save_baseline_jpeg(img, "bootstrap_images/portrait.jpg")

# Create square image (pattern)
print("Creating square.jpg...")
colors = [(70, 130, 180), (255, 165, 0), (50, 205, 50), (220, 20, 60)]
img = create_pattern_image(200, 200, colors)
save_baseline_jpeg(img, "bootstrap_images/square.jpg")

# Create wallpaper image (ocean colors)
print("Creating wallpaper.jpg...")
img = create_gradient_image(400, 300, (0, 119, 182), (0, 45, 90), 'vertical')
save_baseline_jpeg(img, "bootstrap_images/wallpaper.jpg")

print("\n✓ All baseline JPEG images created!")
print("These should work with the picojpeg decoder.")
