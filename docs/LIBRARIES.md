# Vib-OS Third-Party Libraries

This document provides an overview of the third-party libraries integrated into Vib-OS.

## Overview

Vib-OS uses carefully selected, embedded-friendly libraries that are:
- **Lightweight**: Small code footprint
- **Portable**: No external dependencies
- **Well-licensed**: Open-source with permissive licenses
- **Battle-tested**: Widely used in production

## Media Libraries

### 1. minimp3 (CC0 - Public Domain)
**Purpose**: MP3 audio decoding

- **Size**: ~10KB (single header)
- **Location**: `kernel/media/minimp3.h`, `kernel/media/minimp3_ex.h`
- **Source**: https://github.com/lieff/minimp3
- **Features**:
  - Decode MP3 to PCM
  - No external dependencies
  - Fast and efficient
  - Perfect for embedded systems

**Usage**:
```c
media_audio_t audio;
media_decode_mp3(mp3_data, mp3_size, &audio);
// Use audio.samples, audio.sample_rate, audio.channels
media_free_audio(&audio);
```

### 2. picojpeg (Public Domain)
**Purpose**: JPEG image decoding

- **Size**: ~15KB
- **Location**: `kernel/media/picojpeg.c`, `kernel/media/picojpeg.h`
- **Source**: https://github.com/richgel999/picojpeg
- **Features**:
  - Tiny JPEG decoder
  - Optimized for embedded systems
  - Progressive JPEG support
  - Low memory footprint

**Usage**:
```c
media_image_t image;
media_decode_jpeg(jpeg_data, jpeg_size, &image);
// Use image.pixels, image.width, image.height
media_free_image(&image);
```

### 3. stb_image (MIT / Public Domain)
**Purpose**: Universal image loading

- **Size**: ~276KB (single header)
- **Location**: `kernel/include/media/stb_image.h`
- **Implementation**: `kernel/media/stb_image_impl.c`
- **Source**: https://github.com/nothings/stb
- **Supported Formats**:
  - JPEG (baseline & progressive)
  - PNG (8/16-bit-per-channel)
  - BMP (non-1bpp, non-RLE)
  - TGA
  - PSD (composited view only)
  - GIF
  - HDR (radiance rgbE format)
  - PIC (Softimage PIC)
  - PNM (PPM and PGM binary only)

**Features**:
- Auto-detects image format
- Converts to 32-bit RGBA
- No external dependencies
- Thread-safe (with proper allocator)

**Usage**:
```c
media_image_t image;
media_decode_image(data, size, &image);  // Auto-detect format
// OR
media_decode_png(data, size, &image);    // Force PNG
media_decode_bmp(data, size, &image);    // Force BMP
// Use image.pixels (32-bit RGBA)
media_free_image(&image);
```

### 4. stb_image_write (MIT / Public Domain)
**Purpose**: Image encoding/writing

- **Size**: ~70KB (single header)
- **Location**: `kernel/include/media/stb_image_write.h`
- **Source**: https://github.com/nothings/stb
- **Supported Formats**:
  - PNG
  - BMP
  - TGA
  - JPEG (quality configurable)

**Usage**:
```c
uint8_t *png_data;
size_t png_size;
media_encode_png(&image, &png_data, &png_size);
// Write png_data to file or network
kfree(png_data);
```

## Cryptography Libraries

### 5. TLSe (BSD 2-Clause)
**Purpose**: TLS 1.2/1.3 implementation

- **Size**: ~491KB
- **Location**: `kernel/crypto/tlse.c`, `kernel/include/crypto/tlse.h`
- **Wrapper**: `kernel/crypto/tls_wrapper.c`, `kernel/include/crypto/tls.h`
- **Source**: https://github.com/eduardsui/tlse
- **Features**:
  - TLS 1.2 and 1.3 support
  - Client and server modes
  - No external dependencies (no OpenSSL)
  - Small code footprint
  - Perfect for embedded systems
  - SNI (Server Name Indication) support
  - ALPN (Application-Layer Protocol Negotiation)

**Supported Cipher Suites**:
- TLS_AES_128_GCM_SHA256
- TLS_AES_256_GCM_SHA384
- TLS_CHACHA20_POLY1305_SHA256
- TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256
- TLS_ECDHE_RSA_WITH_AES_256_GCM_SHA384
- And more...

**Usage**:
```c
// Initialize TLS
tls_init();

// Create client context
struct tls_context *tls = tls_create_context(0);

// Set hostname for SNI
tls_set_hostname(tls, "example.com");

// Set socket
tls_set_socket(tls, socket_fd);

// Perform handshake
if (tls_handshake(tls) == 0) {
    // Send encrypted data
    tls_write(tls, data, len);
    
    // Receive encrypted data
    tls_read(tls, buffer, sizeof(buffer));
}

// Close connection
tls_close(tls);
```

**Server Usage**:
```c
// Create server context
struct tls_context *tls = tls_create_context(1);

// Load certificate and key
tls_load_certificate(tls, "/etc/ssl/cert.pem", "/etc/ssl/key.pem");

// Accept connection and handshake
tls_set_socket(tls, client_fd);
tls_handshake(tls);

// Handle encrypted communication
tls_read(tls, buffer, sizeof(buffer));
tls_write(tls, response, response_len);

tls_close(tls);
```

## Integration Details

### Memory Management
All libraries use Vib-OS kernel allocator:
```c
#define STBI_MALLOC(sz)           kmalloc(sz, 0)
#define STBI_REALLOC(p,newsz)     krealloc(p, newsz, 0)
#define STBI_FREE(p)              kfree(p)
```

### Thread Safety
- All libraries are thread-safe when using separate contexts
- Shared contexts require external synchronization

### Error Handling
All functions return negative error codes on failure:
```c
int result = media_decode_image(data, size, &image);
if (result < 0) {
    printk(KERN_ERR "Failed to decode image: %d\n", result);
}
```

### Performance Considerations

| Library | Decode Speed | Memory Usage | CPU Usage |
|---------|-------------|--------------|-----------|
| minimp3 | Fast | Low | Low |
| picojpeg | Fast | Very Low | Low |
| stb_image | Medium | Medium | Medium |
| TLSe | N/A | Low | Medium |

### Build Integration

Libraries are automatically included in the kernel build:

```makefile
# Media libraries
KERNEL_SOURCES += kernel/media/media.c
KERNEL_SOURCES += kernel/media/picojpeg.c
KERNEL_SOURCES += kernel/media/stb_image_impl.c

# Crypto libraries
KERNEL_SOURCES += kernel/crypto/tlse.c
KERNEL_SOURCES += kernel/crypto/tls_wrapper.c
```

## Future Enhancements

Potential additions:
- **Video**: stb_vorbis for OGG audio
- **Compression**: miniz for ZIP/deflate
- **Fonts**: stb_truetype for TrueType fonts
- **Networking**: lwIP for TCP/IP stack
- **Crypto**: Monocypher for modern crypto primitives

## License Compliance

All libraries are properly licensed and documented in `THIRD_PARTY_LICENSES.md`:
- ✅ CC0 (Public Domain) - minimp3
- ✅ Public Domain - picojpeg
- ✅ MIT / Public Domain - stb_image, stb_image_write
- ✅ BSD 2-Clause - TLSe

## Testing

Test the libraries:
```bash
# Build with all libraries
make clean
make

# Test in QEMU
make run-gui
```

## Resources

- [stb libraries](https://github.com/nothings/stb) - Sean Barrett's single-header libraries
- [minimp3](https://github.com/lieff/minimp3) - Minimalistic MP3 decoder
- [TLSe](https://github.com/eduardsui/tlse) - Small TLS implementation
- [picojpeg](https://github.com/richgel999/picojpeg) - Tiny JPEG decoder

## Contributing

When adding new libraries:
1. Ensure license compatibility
2. Document in `THIRD_PARTY_LICENSES.md`
3. Add usage examples
4. Update this document
5. Test thoroughly
