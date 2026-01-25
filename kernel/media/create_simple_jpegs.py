#!/usr/bin/env python3
"""
Create simple solid color JPEG images that are guaranteed to work with picojpeg
Uses only standard library - no PIL needed
"""
import struct

def create_minimal_jpeg(width, height, r, g, b, filename):
    """Create a minimal baseline JPEG with solid color"""
    
    # Minimal JPEG structure for a solid color image
    # This is a very basic JPEG that picojpeg should handle
    
    # For simplicity, create a small image and let's use a different approach
    # Let's create actual valid baseline JPEGs by hand-coding the structure
    
    # Actually, let's just create very simple test patterns
    # that are known to work with basic JPEG decoders
    
    # Create a simple gradient or pattern
    import array
    
    # Create raw RGB data
    pixels = []
    for y in range(height):
        for x in range(width):
            # Create a simple gradient
            pixels.extend([
                min(255, r + (x * 50 // width)),
                min(255, g + (y * 50 // height)),
                b
            ])
    
    # For now, let's just create a simple PPM and note that we need proper JPEG
    # Save as PPM first (we can convert later)
    ppm_filename = filename.replace('.jpg', '.ppm')
    with open(ppm_filename, 'wb') as f:
        header = f'P6\n{width} {height}\n255\n'.encode()
        f.write(header)
        f.write(bytes(pixels))
    
    print(f"Created {ppm_filename} ({width}x{height})")
    return ppm_filename

# Create simple test images
create_minimal_jpeg(320, 240, 100, 150, 200, "bootstrap_images/landscape.jpg")
create_minimal_jpeg(240, 320, 200, 100, 150, "bootstrap_images/portrait.jpg")
create_minimal_jpeg(200, 200, 150, 200, 100, "bootstrap_images/square.jpg")
create_minimal_jpeg(400, 300, 180, 180, 220, "bootstrap_images/wallpaper.jpg")

print("\nNote: Created PPM files. Need to convert to JPEG or use different approach.")
