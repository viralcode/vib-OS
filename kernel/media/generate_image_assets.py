#!/usr/bin/env python3
"""
Generate C header files from image assets
"""
import os
import sys

def generate_header(input_file, output_file, var_name):
    """Convert image file to C header - just data, no header guards"""
    with open(input_file, 'rb') as f:
        data = f.read()
    
    with open(output_file, 'w') as f:
        f.write(f"/* Auto-generated from {os.path.basename(input_file)} */\n")
        f.write(f"const unsigned char {var_name}[] = {{\n")
        
        # Write bytes in rows of 16
        for i in range(0, len(data), 16):
            chunk = data[i:i+16]
            hex_str = ', '.join(f'0x{b:02x}' for b in chunk)
            f.write(f"    {hex_str},\n")
        
        f.write("};\n\n")
        f.write(f"const unsigned int {var_name}_len = {len(data)};\n")
    
    print(f"Generated {output_file} ({len(data)} bytes)")

def main():
    images_dir = "kernel/media/bootstrap_images"
    output_dir = "kernel/media"
    
    images = [
        ("landscape.jpg", "bootstrap_landscape_jpg"),
        ("portrait.jpg", "bootstrap_portrait_jpg"),
        ("square.jpg", "bootstrap_square_jpg"),
        ("wallpaper.jpg", "bootstrap_wallpaper_jpg"),
        ("nature.jpg", "bootstrap_nature_jpg"),
        ("city.jpg", "bootstrap_city_jpg"),
        ("httpbin.jpg", "bootstrap_httpbin_jpg"),
    ]
    
    for img_file, var_name in images:
        input_path = os.path.join(images_dir, img_file)
        output_path = os.path.join(output_dir, f"{var_name}.c")
        
        if os.path.exists(input_path):
            generate_header(input_path, output_path, var_name)
        else:
            print(f"Warning: {input_path} not found")

if __name__ == "__main__":
    main()
