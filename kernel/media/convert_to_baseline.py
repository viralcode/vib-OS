#!/usr/bin/env python3
"""
Convert JPEG images to baseline format (no EXIF, no progressive)
for compatibility with picojpeg decoder
"""
from PIL import Image
import os

images_dir = "bootstrap_images"

images = ["landscape.jpg", "portrait.jpg", "square.jpg", "wallpaper.jpg"]

for img_name in images:
    img_path = os.path.join(images_dir, img_name)
    if not os.path.exists(img_path):
        print(f"Warning: {img_path} not found")
        continue
    
    print(f"Converting {img_name}...")
    
    # Open image
    img = Image.open(img_path)
    
    # Convert to RGB if needed (remove alpha channel)
    if img.mode != 'RGB':
        img = img.convert('RGB')
    
    # Save as baseline JPEG (no progressive, no optimize, strip EXIF)
    img.save(img_path, 'JPEG', quality=85, optimize=False, progressive=False, exif=b'')
    
    print(f"  ✓ Converted {img_name} to baseline JPEG")

print("\nAll images converted successfully!")
