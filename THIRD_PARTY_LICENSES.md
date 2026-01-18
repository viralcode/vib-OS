# Third-Party Libraries and Licenses

This project uses the following third-party libraries:

## Media Libraries

### minimp3
- **Purpose**: MP3 audio decoding
- **Location**: `kernel/media/minimp3.h`, `kernel/media/minimp3_ex.h`
- **License**: CC0 (Public Domain)
- **Source**: https://github.com/lieff/minimp3
- **Description**: Minimalistic, single-header MP3 decoder

### picojpeg
- **Purpose**: JPEG image decoding
- **Location**: `kernel/media/picojpeg.c`, `kernel/media/picojpeg.h`
- **License**: Public Domain
- **Source**: https://github.com/richgel999/picojpeg
- **Description**: Tiny JPEG decoder for embedded systems

### stb_image
- **Purpose**: Image loading (PNG, JPEG, BMP, TGA, PSD, GIF, HDR, PIC, PNM)
- **Location**: `kernel/include/media/stb_image.h`
- **License**: MIT / Public Domain
- **Source**: https://github.com/nothings/stb
- **Description**: Single-header image loading library
- **Features**:
  - Supports multiple image formats
  - No external dependencies
  - Easy to integrate

### stb_image_write
- **Purpose**: Image writing (PNG, BMP, TGA, JPEG)
- **Location**: `kernel/include/media/stb_image_write.h`
- **License**: MIT / Public Domain
- **Source**: https://github.com/nothings/stb
- **Description**: Single-header image writing library

## Cryptography Libraries

### TLSe
- **Purpose**: TLS 1.2/1.3 implementation
- **Location**: `kernel/crypto/tlse.c`, `kernel/include/crypto/tlse.h`
- **License**: BSD 2-Clause
- **Source**: https://github.com/eduardsui/tlse
- **Description**: Small, portable TLS 1.2/1.3 implementation
- **Features**:
  - TLS 1.2 and 1.3 support
  - No external dependencies
  - Small code footprint
  - Perfect for embedded systems

## License Texts

### CC0 (Public Domain) - minimp3
```
CC0 1.0 Universal

CREATIVE COMMONS CORPORATION IS NOT A LAW FIRM AND DOES NOT PROVIDE
LEGAL SERVICES. DISTRIBUTION OF THIS DOCUMENT DOES NOT CREATE AN
ATTORNEY-CLIENT RELATIONSHIP. CREATIVE COMMONS PROVIDES THIS
INFORMATION ON AN "AS-IS" BASIS. CREATIVE COMMONS MAKES NO WARRANTIES
REGARDING THE USE OF THIS DOCUMENT OR THE INFORMATION OR WORKS
PROVIDED HEREUNDER, AND DISCLAIMS LIABILITY FOR DAMAGES RESULTING FROM
THE USE OF THIS DOCUMENT OR THE INFORMATION OR WORKS PROVIDED
HEREUNDER.
```

### MIT License - stb_image
```
MIT License

Copyright (c) 2017 Sean Barrett

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

### BSD 2-Clause License - TLSe
```
BSD 2-Clause License

Copyright (c) 2016-2024, Eduard Suica
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```

## Usage in Vib-OS

### Image Loading Example
```c
#include "media/media.h"

// Load any image format (auto-detected)
uint8_t *file_data;
size_t file_size;
media_load_file("/path/to/image.png", &file_data, &file_size);

media_image_t image;
media_decode_image(file_data, file_size, &image);

// Use image.pixels, image.width, image.height

media_free_image(&image);
media_free_file(file_data);
```

### MP3 Decoding Example
```c
#include "media/media.h"

uint8_t *mp3_data;
size_t mp3_size;
media_load_file("/path/to/audio.mp3", &mp3_data, &mp3_size);

media_audio_t audio;
media_decode_mp3(mp3_data, mp3_size, &audio);

// Use audio.samples, audio.sample_count, audio.sample_rate

media_free_audio(&audio);
media_free_file(mp3_data);
```

### TLS Connection Example
```c
#include "crypto/tls.h"

// Initialize TLS
tls_init();

// Create client context
struct tls_context *tls = tls_create_context(0);

// Set hostname for SNI
tls_set_hostname(tls, "example.com");

// Set socket
tls_set_socket(tls, socket_fd);

// Perform handshake
tls_handshake(tls);

// Send/receive data
tls_write(tls, data, len);
tls_read(tls, buffer, sizeof(buffer));

// Close connection
tls_close(tls);
```

## Acknowledgments

We thank the authors and contributors of these excellent libraries for making them available to the open-source community.
