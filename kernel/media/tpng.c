/*
 * tPNG - Minimal PNG decoder for kernel use
 * 
 * Based on tPNG by Johnathan Corkery (https://github.com/jcorks/tPNG)
 * and TINFL by Rich Geldreich.
 *
 * Adapted for Vib-OS kernel - uses kmalloc/kfree instead of libc functions.
 * MIT/Apache-2.0 Licensed - see original source for full license.
 */

#include "tpng.h"
#include "mm/kmalloc.h"
#include "string.h"
#include "printk.h"

/* Kernel-compatible abs macro */
#ifndef abs
#define abs(x) ((x) < 0 ? -(x) : (x))
#endif

/* Kernel-compatible calloc replacement */
static inline void *tpng_kcalloc(size_t n, size_t sz) {
    size_t total = n * sz;
    void *ptr = kmalloc(total, GFP_KERNEL);
    if (ptr) {
        memset(ptr, 0, total);
    }
    return ptr;
}

/* Memory allocation macros using kernel functions */
#define TPNG_MALLOC(sz) kmalloc((sz), GFP_KERNEL)
#define TPNG_FREE(ptr) kfree((ptr))
#define TPNG_CALLOC(n, sz) tpng_kcalloc((n), (sz))
#define realloc(ptr, sz) krealloc((ptr), (sz), 0)

/* Endianness detection (ARM64 is little endian) */
#define TPNG_ENDIANNESS 0

/* Palette size limit as defined by PNG spec */
#define TPNG_PALETTE_LIMIT 256

//
// This follows the PNG specification in version 1.2.
//


// Palette size limit as defined by the spec.
#define TPNG_PALETTE_LIMIT 256



// tPNG iterator 
// Helper class that lets you iterate 
// through a data buffer. 
typedef struct tpng_iter_t tpng_iter_t;

// Creates a new tPNG iterator.
tpng_iter_t * tpng_iter_create(const uint8_t * data, uint32_t size);

// Frees data associated with tPNG.
void tpng_iter_destroy(tpng_iter_t *);

// Returns a read-only buffer of the requested size.
// If the request would make the iterator read out-of-bounds,
// a new, emtpy buffer is returned of that size.
const void * tpng_iter_advance_guaranteed(tpng_iter_t *, uint32_t);

// Returns a read-only buffer of the requested size.
// If the request would make the iterator read out-of-bounds,
// NULL is returned.
const void * tpng_iter_advance(tpng_iter_t *, uint32_t);


// Helper macros for more readable reading 
// of data from tPNG iterator

// Helper macro setup. Takes in the iterator to set up.
// Can only be called once per scope.
#define TPNG_BEGIN(__iter__)tpng_iter_t * TPNGITER=__iter__

// Reads the data type from the iterator.
#define TPNG_READ(__type__)*(__type__*)((tpng_iter_advance_guaranteed(TPNGITER, sizeof(__type__))))

// Reads n bytes from the iterator.
// Needs to be checked for NULL, so call for large amounts 
// of bytes only when needed.
#define TPNG_READ_N(__len__)tpng_iter_advance(TPNGITER, __len__)



// Open structure for accessing a raw PNG 
// file chunk.
typedef struct {
    // The length of the data section.
    uint32_t length;

    // The type of the chunk.
    // Actually 4 bytes, nul terminated though!
    char type[5];

    // The custom data of the chunk, of length "length"
    const uint8_t * data;

    // The CRC. Not used by this implementation.
    int crc; 
} tpng_chunk_t;


// Structure for initial header.
typedef struct {
    uint8_t bytes[8];
} tpng_header_t;


// RGB palette color.
typedef struct {
    // Red component. 0 - 255.
    uint8_t r;

    // Green component. 0 - 255.
    uint8_t g;

    // Blue component. 0 - 255.
    uint8_t b;

    // Alpha component, modified by the tRNS chunk.
    uint8_t a;
} tpng_palette_entry_t;


typedef struct {
    // Whether the current running device is littleEndian.
    // NOTE: its assumed that big endian is the ONLY alternative.
    int littleEndian;

    // Width of the image in pixels.
    int w;

    // Height of the image in pixels
    int h;
    

    // Number of bits per pixel for the color component.
    int colorDepth;

    // The PNG color type.    
    int colorType;
    

    // The compression method.
    int compression;

    // The filter method.
    int filterMethod;

    // Interlacing.
    int interlaceMethod;

    // the transparent gray color.
    // -1 if not used.
    int transparentGray;
    
    // "truecolor" transparency.
    int transparentRed;
    int transparentGreen;
    int transparentBlue;


    // The palette specified by PLTE chunk.
    tpng_palette_entry_t palette[TPNG_PALETTE_LIMIT];

    // number of palette entries that are valid.
    uint32_t nPalette;
    
    // The output rgba.
    uint8_t * rgba;





    // Appended IDAT data, raw and assembled.
    uint8_t * idata;

    // The number of bytes in the raw data section.
    uint32_t idataLength;
} tpng_image_t;



// Populates a raw chunk from the given data iterator.
static void tpng_read_chunk(tpng_image_t *, tpng_iter_t *, tpng_chunk_t * chunk); 

// Interprets the chunk data and applies it.
static void tpng_process_chunk(tpng_image_t * image, tpng_chunk_t * chunk);

// Initializes the image.
static void tpng_image_init(tpng_image_t *, uint32_t rawLen);

// Cleans up any working data needed for computation from init
// or chunk processing.
static void tpng_image_cleanup(tpng_image_t *);










uint8_t * tpng_get_rgba(
    // raw byte data of the PNG file.
    const uint8_t * rawData,

    // Length of the raw data.
    uint32_t        rawSize,


    // Pointer to an editable uint32_t for image width.
    uint32_t * w, 

    // Pointer to an editable uint32_t for image height.
    uint32_t * h
) {
    *w = 0;
    *h = 0;
    

    tpng_chunk_t chunk;
    tpng_image_t image;
    tpng_image_init(&image, rawSize);


    tpng_iter_t * iter = tpng_iter_create(rawData, rawSize);
    TPNG_BEGIN(iter);
    
    // universal PNG header
    {
        tpng_header_t header = TPNG_READ(tpng_header_t);
        if (header.bytes[0] != 137 ||
            header.bytes[1] != 80 ||
            header.bytes[2] != 78 ||
            header.bytes[3] != 71 ||
            
            header.bytes[4] != 13 ||
            header.bytes[5] != 10 ||
            header.bytes[6] != 26 ||
            header.bytes[7] != 10
        ) {
            // not a PNG!
            tpng_image_cleanup(&image);
            tpng_iter_destroy(iter);
            return 0;
        }    
    }

    
    // next read chunks
    tpng_read_chunk(&image, iter, &chunk); 
    tpng_process_chunk(&image, &chunk);
    
    while(strcmp(chunk.type, "IEND")) {
        tpng_read_chunk(&image, iter, &chunk); 
        tpng_process_chunk(&image, &chunk);
    }
      
    tpng_iter_destroy(iter);

      
    // return processed image
    *w = image.w;
    *h = image.h;
    tpng_image_cleanup(&image);
    return image.rgba;
      
}













static int32_t tpng_read_integer(tpng_image_t * img, tpng_iter_t * iter) {
    const uint8_t * src = tpng_iter_advance(iter, sizeof(int32_t));
    if (!src) return 0;
    int i;
    char * data = (char*)&i;
    if (img->littleEndian) {
        data[0] = src[3];
        data[1] = src[2];
        data[2] = src[1];
        data[3] = src[0];                
    } else {
        data[0] = src[0];
        data[1] = src[1];
        data[2] = src[2];
        data[3] = src[3];                
    }
    return i;
}

static uint32_t tpng_read_uinteger(tpng_image_t * img, tpng_iter_t * iter) {
    const uint8_t * src = tpng_iter_advance(iter, sizeof(int32_t));
    if (!src) return 0;
    uint32_t i;
    char * data = (char*)&i;
    if (img->littleEndian) {
        data[0] = src[3];
        data[1] = src[2];
        data[2] = src[1];
        data[3] = src[0];                
    } else {
        data[0] = src[0];
        data[1] = src[1];
        data[2] = src[2];
        data[3] = src[3];                
    }
    return i;
}



static void tpng_read_chunk(tpng_image_t * image, tpng_iter_t * iter, tpng_chunk_t * chunk) {
    TPNG_BEGIN(iter);
    memset(chunk, 0, sizeof(tpng_chunk_t));
    chunk->length = tpng_read_uinteger(image, iter);
    chunk->type[0] = TPNG_READ(char);
    chunk->type[1] = TPNG_READ(char);
    chunk->type[2] = TPNG_READ(char);
    chunk->type[3] = TPNG_READ(char);
    chunk->type[4] = 0;
    chunk->data = TPNG_READ_N(chunk->length);
    // chunkLength was lying! could not read as much as it said...
    if (!chunk->data) {
        chunk->length = 0;
    }
    chunk->crc  = tpng_read_integer(image, iter); // crc 
    
    if (chunk->type[0] == 0 &&
        chunk->type[1] == 0 &&
        chunk->type[2] == 0 &&
        chunk->type[3] == 0) {
        // corruption or early EOF.
        // mark with auto-end chunk
        chunk->type[0] = 'I';   
        chunk->type[1] = 'E';   
        chunk->type[2] = 'N';   
        chunk->type[3] = 'D';   
    }
}

static void tpng_image_init(tpng_image_t * image, uint32_t rawlen) {
    image->idataLength = 0;
    image->idata = TPNG_MALLOC(rawlen); // idata is NEVER longer than the raw files size.
    image->rgba = 0;
    image->colorType = -1;
    image->colorDepth = 0;
    image->transparentGray = -1;
    image->transparentRed = -1;
    int i;
    for(i = 0; i < TPNG_PALETTE_LIMIT; ++i) {
        image->palette[i].a = 255;
    }
    #if (TPNG_ENDIANNESS == -1)
        image->littleEndian = 1;
        image->littleEndian = *((char*)(&image->littleEndian));
    #elif (TPNG_ENDIANNESS == 0)
        image->littleEndian = 1;
    #else
        image->littleEndian = 0;
    #endif
}


static void tpng_expand_row(tpng_image_t * image, const uint8_t * row, uint8_t * expanded, int rowPixelWidth) {
    uint32_t i;
    uint32_t bitCount = image->colorDepth*rowPixelWidth;
    int iter;
    int palette;
    int rawVal, rawG, rawB;
    switch(image->colorType) {
      // grayscale!
      case 0:
        switch(image->colorDepth) {
          case 1:
            for(i = 0; i < bitCount; ++i, expanded+=4) {
                // raw val is: the i'thbit, starting from MSB
                rawVal = ((row[i/8] >> (7 - i%8)) & 1);
                *expanded   = rawVal * 255; 
                expanded[1] = *expanded;
                expanded[2] = *expanded;
                expanded[3] = 255;
                if (image->transparentGray == rawVal) {
                    expanded[3] = 0;
                }
            }
            break;
          case 2:
            for(i = 0; i < bitCount; i+=2, expanded+=4) {
                // raw val is: the i'th 2bits, starting from MSB
                rawVal = ((row[i/8] >> (6 - i%8)) & 3);
                *expanded = (rawVal/3.0) * 255; 
                expanded[1] = *expanded;
                expanded[2] = *expanded;
                expanded[3] = 255;
                if (image->transparentGray == rawVal) {
                    expanded[3] = 0;
                }
            }
            break;
          case 4:
            for(i = 0; i < bitCount; i+=4, expanded+=4) {
                // raw val is: the i'th 4bits, starting from MSB
                rawVal = ((row[i/8] >> (4-i%8)) & 15);
                *expanded   = (rawVal/15.0) * 255; 
                expanded[1] = *expanded;
                expanded[2] = *expanded;
                expanded[3] = 255;
                if (image->transparentGray == rawVal) {
                    expanded[3] = 0;
                }
            }
            break;
          case 8:
            for(i = 0; i < bitCount; i+=8, expanded+=4) {
                *expanded =   row[i/8]; 
                expanded[1] = *expanded;
                expanded[2] = *expanded;
                expanded[3] = 255;
                if (image->transparentGray == *expanded) {
                    expanded[3] = 0;
                }

            }
            break;
          case 16:
            for(i = 0; i < bitCount; i+=16, expanded+=4) {
                rawVal = (0xff*row[i/8]+row[i/8+1]);
                *expanded = row[i/8]; 
                expanded[1] = *expanded;
                expanded[2] = *expanded;
                expanded[3] = 255;
                if (image->transparentGray == rawVal) {
                    expanded[3] = 0;
                }                
            }
            break;

          
          default:;
        }     
        break;


        
        
        
       // plain RGB!
       case 2:
        iter = 0;
        switch(image->colorDepth) {
          case 8:
            for(i = 0; i < rowPixelWidth; ++i, iter+=4) {
                expanded[iter]   = row[i*3  ]; 
                expanded[iter+1] = row[i*3+1]; 
                expanded[iter+2] = row[i*3+2]; 
                expanded[iter+3] = 255;
                if (image->transparentRed   == expanded[iter] &&
                    image->transparentGreen == expanded[iter+1] &&
                    image->transparentBlue  == expanded[iter+2]) {
                    expanded[iter+3] = 0;
                }
            }
            break;
          case 16:
            for(i = 0; i < rowPixelWidth; ++i, iter+=4) {
                rawVal = row[i*6]  *0xff + row[i*6+1];
                rawG =   row[i*6+2]*0xff + row[i*6+3];
                rawB =   row[i*6+4]*0xff + row[i*6+5];

                expanded[iter]   = row[i*6]; 
                expanded[iter+1] = row[i*6+2]; 
                expanded[iter+2] = row[i*6+4]; 
                expanded[iter+3] = 255;
                if (image->transparentRed   == rawVal &&
                    image->transparentGreen == rawG &&
                    image->transparentBlue  == rawB) {
                    expanded[iter+3] = 0;
                }

            }
            break;
          default:;  
        }
        break;  
            
      // palette!
      case 3:
        iter = 0;
        switch(image->colorDepth) {
          case 1:
            for(i = 0; i < bitCount; ++i, iter+=4) {
                palette = ((row[i/8] >> (7 - i%8)) & 1);
                expanded[iter]   = image->palette[palette].r; 
                expanded[iter+1] = image->palette[palette].g; 
                expanded[iter+2] = image->palette[palette].b; 
                expanded[iter+3] = image->palette[palette].a;
            }
            break;
          case 2:
            for(i = 0; i < bitCount; i+=2, iter+=4) {
                palette = ((row[i/8] >> (6 - i%8)) & 3);
                expanded[iter]   = image->palette[palette].r; 
                expanded[iter+1] = image->palette[palette].g; 
                expanded[iter+2] = image->palette[palette].b; 
                expanded[iter+3] = image->palette[palette].a;
            }
            break;
          case 4:
            for(i = 0; i < bitCount; i+=4, iter+=4) {
                palette = ((row[i/8] >> (4-i%8)) & 15);
                expanded[iter]   = image->palette[palette].r; 
                expanded[iter+1] = image->palette[palette].g; 
                expanded[iter+2] = image->palette[palette].b; 
                expanded[iter+3] = image->palette[palette].a;
            }
            break;
          case 8:
            for(i = 0; i < bitCount; i+=8, iter+=4) {
                palette = row[i/8];
                expanded[iter]   = image->palette[palette].r; 
                expanded[iter+1] = image->palette[palette].g; 
                expanded[iter+2] = image->palette[palette].b; 
                expanded[iter+3] = image->palette[palette].a;
            }
            break;
          default:;
        }       
        break;
            
            
            
            
            
      // grayscale + alpha!
      case 4:
        switch(image->colorDepth) {
          case 8:
            for(i = 0; i < rowPixelWidth; ++i) {
                expanded[i*4]   = row[i*2]; 
                expanded[i*4+1] = row[i*2]; 
                expanded[i*4+2] = row[i*2]; 
                expanded[i*4+3] = row[i*2+1]; 

            }
            break;
          case 16:
            for(i = 0; i < rowPixelWidth; ++i) {
                expanded[i*4]   = row[i*4]; 
                expanded[i*4+1] = expanded[i*4+0];
                expanded[i*4+2] = expanded[i*4+1];
                expanded[i*4+3] = row[i*4+2]; 
            }
            break;
        }
        break;           

      // RGBA!
      case 6:
        switch(image->colorDepth) {
          case 8:
            for(i = 0; i < rowPixelWidth; ++i) {
                expanded[i*4  ] = row[i*4  ]; 
                expanded[i*4+1] = row[i*4+1]; 
                expanded[i*4+2] = row[i*4+2]; 
                expanded[i*4+3] = row[i*4+3]; 
            }
            break;
          case 16:
            for(i = 0; i < rowPixelWidth; ++i) {
                expanded[i*4  ] = row[i*8]  ; 
                expanded[i*4+1] = row[i*8+2]; 
                expanded[i*4+2] = row[i*8+4]; 
                expanded[i*4+3] = row[i*8+6]; 
            }
            break;
          default:;  
        }
        break;

      default:;
    }
}


static int tpng_paeth_predictor(int a, int b, int c) {
    int p = a + b - c;// checked, no overflow
    int pa = abs(p-a);// checked, no overflow
    int pb = abs(p-b);// checked, no overflow
    int pc = abs(p-c);// checked, no overflow

    if      (pa <= pb && pa <= pc) return a;
    else if (pb <= pc)             return b;
    else                           return c;
}


static void tpng_unfilter_row(
    tpng_image_t  * image, 
    uint8_t       * thisRow,
    const uint8_t * prevRow,
    uint32_t        rowBytes,
    int             Bpp,
    int             filter
) {
    uint32_t i;
    switch(filter) {
      case 0: // no filtering 
        break;
      case 1: // Sub 
        for(i = Bpp; i < rowBytes; ++i) {
            thisRow[i] = thisRow[i] + thisRow[i-Bpp];
        }
        break;
      case 2: // Up
        for(i = 0; i < rowBytes; ++i) {
            thisRow[i] = thisRow[i] + prevRow[i];
        }
        break;

      case 3: //average
        for(i = 0; i < Bpp; ++i) {
            thisRow[i] = thisRow[i] + (int)((0 + prevRow[i])/2.0);
        }
        for(i = Bpp; i < rowBytes; ++i) {
            thisRow[i] = thisRow[i] + (int)((thisRow[i-Bpp] + prevRow[i])/2.0);
        }
        break;   

      case 4: //paeth
        for(i = 0; i < Bpp; ++i) {           
            thisRow[i] = thisRow[i] + tpng_paeth_predictor(0, prevRow[i], 0);
        }
        for(i = Bpp; i < rowBytes; ++i) {
            thisRow[i] = thisRow[i] + tpng_paeth_predictor(thisRow[i-Bpp], prevRow[i], prevRow[i-Bpp]);
        }
        break;
      default:;
    }      
}

static int tpng_get_bytes_per_pixel(tpng_image_t * image) {
    int bpp = image->colorDepth;

    // R G B per pixel
    if (image->colorType == 2||
        image->colorType == 6) {
        bpp *= 3;
    }

    // alpha channel
    if (image->colorType & 4) {
        bpp += image->colorDepth;
    }


    return bpp < 8 ? 1 : bpp/8;
}   

static int tpng_get_bytes_per_row(tpng_image_t * image, int width) {
    int bpp = image->colorDepth;

    // R G B per pixel
    if (image->colorType == 2||
        image->colorType == 6) {
        bpp *= 3;
    }

    // alpha channel
    if (image->colorType & 4) {
        bpp += image->colorDepth;
    }

    bpp *= width;

    if (bpp < 8) return 1;
    if (bpp % 8) {
        return bpp/8 + 1;
    } else {
        return bpp/8;
    }
}


//TODO: adam7 needs to serious efficiency updates!
// its mostly written rn for readability.

// bytes within the 8x8 adam7 grid
// that each pass makes
static uint8_t TPNG_ADAM7__PASS_1_BYTES[] = {0};
static uint8_t TPNG_ADAM7__PASS_2_BYTES[] = {4};
static uint8_t TPNG_ADAM7__PASS_3_BYTES[] = {32, 36}; // Pass3
static uint8_t TPNG_ADAM7__PASS_4_BYTES[] = {
    2, 6,
    34, 38
};
    
static uint8_t TPNG_ADAM7__PASS_5_BYTES[] = {
    16, 18, 20, 22,
    48, 50, 52, 54        
};


static uint8_t TPNG_ADAM7__PASS_6_BYTES[] = {
    1,  3,  5,  7,
    17, 19, 21, 23,
    33, 35, 37, 39,
    49, 51, 53, 55
};

static uint8_t TPNG_ADAM7__PASS_7_BYTES[] = {
    8,  9,  10, 11, 12, 13, 14, 15,
    24, 25, 26, 27, 28, 29, 30, 31,
    40, 41, 42, 43, 44, 45, 46, 47,
    56, 57, 58, 59, 60, 61, 62, 63        
};

static uint8_t * TPNG_ADAM7__PASS_BYTES[] = {
    TPNG_ADAM7__PASS_1_BYTES,
    TPNG_ADAM7__PASS_2_BYTES,
    TPNG_ADAM7__PASS_3_BYTES,    
    TPNG_ADAM7__PASS_4_BYTES,
    TPNG_ADAM7__PASS_5_BYTES,
    TPNG_ADAM7__PASS_6_BYTES,
    TPNG_ADAM7__PASS_7_BYTES
};



// width, height of each pass subimage
static uint8_t TPNG_ADAM7__PASS_DIMS[][2] = {
    {1, 1}, // pass 1
    {1, 1}, // pass 2,
    {2, 1}, // pass 3,
    {2, 2}, // pass 4,
    {4, 2}, // pass 5,
    {4, 4}, // pass 6,
    {8, 4}, // pass 7
};


// given a pixel in a pass subimage, returns the index 
// in the image RGBA buffer.
static int tpng_adam7_subpixel_to_pixel(
    tpng_image_t * image,
    // x and y in the SUBIMAGE, which is a concatenation of adam7 boxes
    // for a specific pass.
    int subx,
    int suby,
    int pass // z
) {
    // x / y in adam7 box space
    int x8 = subx%TPNG_ADAM7__PASS_DIMS[pass][0];
    int y8 = suby%TPNG_ADAM7__PASS_DIMS[pass][1];

    const uint8_t * passBytes = TPNG_ADAM7__PASS_BYTES[pass];   

    // actual adam7 box row
    const uint8_t * passRow = passBytes+TPNG_ADAM7__PASS_DIMS[pass][0]*y8;
    
    // byte reference within the adam7 box
    int adamByte = passRow[x8];


    // offset from row start in full image rgba
    int x = adamByte%8   + 8*(subx/TPNG_ADAM7__PASS_DIMS[pass][0]);
    int y = (adamByte/8) + 8*(suby/TPNG_ADAM7__PASS_DIMS[pass][1]);
    
    //printf("Pass%d: (%d, %d) -> (%d, %d) @%d\n", pass, subx, suby, x, y, adamByte);
    return x + y*image->w;
}

static void tpng_adam7_pass_row_to_image(
    const uint8_t * passRgbaRow,
    tpng_image_t * image,
    int subrow,
    int rowWidth,
    int pass
) {
    uint32_t i;
    int pixel;
    for(i = 0; i < rowWidth; ++i) {
        pixel = tpng_adam7_subpixel_to_pixel(image, i, subrow, pass);
        // should be fine; theyre aligned to 32bit boundaries.
        (*(uint32_t*)(image->rgba+(pixel*4))) = (*(uint32_t*)(passRgbaRow+(i*4))); 
    }
    
}

// gets how many pixels fit from an adam7 pass width-wise
static int tpng_adam7_get_pass_width(tpng_image_t * image, int pass) {
    int width = 0; 
    int i, n;
    int subimgW = TPNG_ADAM7__PASS_DIMS[pass][0];
    const uint8_t * iter = TPNG_ADAM7__PASS_BYTES[pass];
    for(i = 0; i < image->w; i+=8) {    
        for(n = 0; n < subimgW; ++n) {
            if (i+(iter[n]%8) < image->w)
                width++;
            else 
                return width;
        }
    }
    return width;
}

static int tpng_adam7_get_pass_height(tpng_image_t * image, int pass) {
    int height = 0; 
    int i, n;
    int subimgH = TPNG_ADAM7__PASS_DIMS[pass][1];
    int subimgW = TPNG_ADAM7__PASS_DIMS[pass][0];
    const uint8_t * iter = TPNG_ADAM7__PASS_BYTES[pass];
    for(i = 0; i < image->h; i+=8) {    
        for(n = 0; n < subimgH; ++n) {
            if (i+(iter[n*subimgW]/8) < image->h)
                height++;
            else 
                return height;
        }
    }
    return height;
}




static void tpng_adam7_decode(
    tpng_image_t * image, 
    tpng_iter_t * iter,
    int Bpp  
) {
    TPNG_BEGIN(iter);
    uint32_t rowBytes = tpng_get_bytes_per_row(image, image->w);

    // row bytes of the above row, filter byte discarded
    // Before initialized, is 0.
    uint8_t * prevRow = TPNG_CALLOC(1, rowBytes);
    // row bytes of the current row, filter byte discarded
    uint8_t * thisRow = TPNG_CALLOC(1, rowBytes);

    // Expanded raw row, where each RGBA pixel is given 
    // the raw value within 
    uint8_t * rowExpanded = TPNG_CALLOC(4, image->w);



    // each pass is an independent subimage that correspond 
    // to pixels within the final image, separated in 8x8 
    // chunks.
    int pass;

    // the width and height for the pass.
    int passWidth;
    int passHeight;
    int passRowBytes;
    int row;

    


    for(pass = 0; pass < 7; ++pass) {
        memset(prevRow, 0, rowBytes);
        
        // first, we need how many rows / bytes consist of this row.
        passWidth  = tpng_adam7_get_pass_width(image, pass);
        passHeight = tpng_adam7_get_pass_height(image, pass);
        
        if (passWidth == 0) continue;
        passRowBytes  = tpng_get_bytes_per_row(image, passWidth);
        
        for(row = 0; row < passHeight; ++row) {
            int filter = TPNG_READ(uint8_t);
            const void * readN = TPNG_READ_N(passRowBytes);
            // abort read of IDAT
            if (!readN) break;
            memcpy(thisRow, readN, passRowBytes);
    
      
            // remove the filter from the bytes in the row 
            tpng_unfilter_row(image, thisRow, prevRow, passRowBytes, Bpp, filter);

            // finally: get scanlines from data
            tpng_expand_row(image, thisRow, rowExpanded, passWidth);


            tpng_adam7_pass_row_to_image(
                rowExpanded,
                image,
                row, // row within the complete pass image
                passWidth,
                pass
            );
            
            
            // save raw previous scanline            
            memcpy(prevRow, thisRow, rowBytes);
        }
    }

    
    TPNG_FREE(prevRow);
    TPNG_FREE(thisRow);
    TPNG_FREE(rowExpanded);
    
}



// zlib decompression, from TINFL
static void *tinfl_decompress_mem_to_heap(
    const void *pSrc_buf, 
    size_t src_buf_len, 
    size_t *pOut_len, 
    int flags
);

static void tpng_process_chunk(tpng_image_t * image, tpng_chunk_t * chunk) {

    // Header. SHOULD always be first.
    if (!strcmp(chunk->type, "IHDR")) {
        tpng_iter_t * iter = tpng_iter_create(chunk->data, chunk->length);
        TPNG_BEGIN(iter);
        image->w = tpng_read_integer(image, iter);
        image->h = tpng_read_integer(image, iter);
        
        image->colorDepth      = TPNG_READ(char);
        image->colorType       = TPNG_READ(char);
        image->compression     = TPNG_READ(char);
        image->filterMethod    = TPNG_READ(char);
        image->interlaceMethod = TPNG_READ(char);
        
        image->rgba = TPNG_CALLOC(4, image->w*image->h);

        tpng_iter_destroy(iter);


    // Nice palette
    } else if (!strcmp(chunk->type, "PLTE")) {
        tpng_iter_t * iter = tpng_iter_create(chunk->data, chunk->length);
        TPNG_BEGIN(iter);
        
        image->nPalette = chunk->length / 3;
        if (image->nPalette > TPNG_PALETTE_LIMIT) image->nPalette = TPNG_PALETTE_LIMIT;
        uint32_t i;
        for(i = 0; i < image->nPalette; ++i) {
            image->palette[i].r = TPNG_READ(uint8_t);
            image->palette[i].g = TPNG_READ(uint8_t);
            image->palette[i].b = TPNG_READ(uint8_t);
        }
        tpng_iter_destroy(iter);        

    // Raw image data. We never process independently, we 
    // always assemble.
    } else if (!strcmp(chunk->type, "IDAT")) {
        memcpy(image->idata+image->idataLength, chunk->data, chunk->length);         
        image->idataLength += chunk->length;

    // Simple transparency!
    } else if (!strcmp(chunk->type, "tRNS")) {
        tpng_iter_t * iter = tpng_iter_create(chunk->data, chunk->length);
        TPNG_BEGIN(iter);

        // palette transparency
        if (image->colorType == 3) {
            uint32_t i;
            for(i = 0; i < chunk->length && i < TPNG_PALETTE_LIMIT; ++i) {
                image->palette[i].a = TPNG_READ(uint8_t);
            }

        // grayscale
        } else if (image->colorType == 0) {
            // network byte order!
            image->transparentGray = TPNG_READ(uint8_t)*0xff + TPNG_READ(uint8_t);

        // 
        } else if (image->colorType == 2) {
            image->transparentRed   = TPNG_READ(uint8_t)*0xff + TPNG_READ(uint8_t);
            image->transparentGreen = TPNG_READ(uint8_t)*0xff + TPNG_READ(uint8_t);
            image->transparentBlue  = TPNG_READ(uint8_t)*0xff + TPNG_READ(uint8_t);            
        }
        tpng_iter_destroy(iter);        

    } else if (!strcmp(chunk->type, "IEND")) {
        // compression mode is the current and only accepted type.
        if (image->compression != 0) return;

        // Invalid file: missing IHDR chunk, OR IHDR comes 
        // in an invalid order.
        if (!image->rgba) return;

        // now safe to work with IDAT input
        // first: decompress (inflate)
        size_t rawUncompLen;
        void * rawUncomp = tinfl_decompress_mem_to_heap(
            image->idata, 
            image->idataLength, 
            &rawUncompLen, 
            1 // actually contains zlib header
        );
        tpng_iter_t * iter = tpng_iter_create(rawUncomp, rawUncompLen);
        TPNG_BEGIN(iter);        

        
        
        // next: filter
        // each scanline contains a single byte specifying how its filtered (reordered)
        uint32_t row = 0;
        int Bpp = tpng_get_bytes_per_pixel(image);
        
        if (image->interlaceMethod == 0) {
            uint32_t rowBytes = tpng_get_bytes_per_row(image, image->w);

            // row bytes of the above row, filter byte discarded
            // Before initialized, is 0.
            uint8_t * prevRow = TPNG_CALLOC(1, rowBytes);
            // row bytes of the current row, filter byte discarded
            uint8_t * thisRow = TPNG_CALLOC(1, rowBytes);


            // Expanded raw row, where each RGBA pixel is given 
            // the raw value within 
            uint8_t * rowExpanded = TPNG_CALLOC(4, image->w);
            for(row = 0; row < image->h; ++row) {
                int filter = TPNG_READ(uint8_t);
                const void * readN = TPNG_READ_N(rowBytes);
                // abort read of IDAT
                if (!readN) break;
                memcpy(thisRow, readN, rowBytes);
        
          
                // remove the filter from the bytes in the row 
                tpng_unfilter_row(image, thisRow, prevRow, rowBytes, Bpp, filter);

                // finally: get scanlines from data
                tpng_expand_row(image, thisRow, rowExpanded, image->w);
                
                memcpy(
                    image->rgba + row*image->w*4, 
                    rowExpanded,
                    4 * (image->w)
                );
                    
                // save raw previous scanline            
                memcpy(prevRow, thisRow, rowBytes);
            }
            TPNG_FREE(thisRow);
            TPNG_FREE(prevRow);
            TPNG_FREE(rowExpanded);
            // adam7..
        } else if (image->interlaceMethod == 1) {
            tpng_adam7_decode(image, iter, Bpp);        
        }            
        tpng_iter_destroy(iter);        
        TPNG_FREE(rawUncomp);
    }
}



static void tpng_image_cleanup(tpng_image_t * image) {
    TPNG_FREE(image->idata);
}













// tpng iterator helper class
///////////////
struct tpng_iter_t {
    // Raw data, read-only.
    const uint8_t * data;

    // (safe) size of the read-only buffer
    uint32_t        size;

    // where we are within the iterator.
    uint32_t        iter;

    // buffers saved to process invalid requests.
    uint8_t **      errors;

    // Number of error buffers.
    uint32_t        nerrors;
};

tpng_iter_t * tpng_iter_create(const uint8_t * data, uint32_t size) {
    tpng_iter_t * out = TPNG_CALLOC(1, sizeof(tpng_iter_t));
    out->data = data;
    out->size = size;
    out->iter = 0;
    
    out->nerrors = 0;
    out->errors = NULL;
    return out;
}

void tpng_iter_destroy(tpng_iter_t * t) { 
    uint32_t i;
    for(i = 0; i < t->nerrors; ++i) {
        TPNG_FREE(t->errors[i]);
    }
    TPNG_FREE(t->errors);
    TPNG_FREE(t);
}

#ifdef TPNG_REPORT_ERROR
    #include <stdio.h>
#endif

const void * tpng_iter_advance(tpng_iter_t * t, uint32_t size) {
    if (!size) return NULL;
    if (t->iter + size <= t->size) {
        const void * out = t->data+t->iter;     
        t->iter+=size;   
        return out;
    } 
    return NULL;
}

const void * tpng_iter_advance_guaranteed(tpng_iter_t * t, uint32_t size) {
    const void * data = tpng_iter_advance(t, size);
    if (!data) {
        // no partial requests. 
        // An entire empty buffer is returned for bad requests.
        #ifdef TPNG_REPORT_ERROR
            printf("tPNG: WARNING: Request denied to read %d bytes, which is %d bytes past %p\n", size, (size + t->iter) - t->size, t->data); 
        #endif
        void * newError = TPNG_CALLOC(1, size);
        void * newErrorArray = TPNG_MALLOC(sizeof(void*)*(t->nerrors+1));
        if (t->nerrors) {
            memcpy(newErrorArray, t->errors, sizeof(void*)*t->nerrors);
            TPNG_FREE(t->errors);
        }
        t->errors = newErrorArray;
        t->errors[t->nerrors] = newError;
        t->nerrors++;
        return newError;
    } else {
        return data;
    }
}


///////////////








/////////////////////
/////// BEGIN TINFL 
///
/**************************************************************************
 *
 * Copyright 2013-2014 RAD Game Tools and Valve Software
 * Copyright 2010-2014 Rich Geldreich and Tenacious Software LLC
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 **************************************************************************/



/* Decompression flags used by tinfl_decompress(). */
/* TINFL_FLAG_PARSE_ZLIB_HEADER: If set, the input has a valid zlib header and ends with an adler32 checksum (it's a valid zlib stream). Otherwise, the input is a raw deflate stream. */
/* TINFL_FLAG_HAS_MORE_INPUT: If set, there are more input bytes available beyond the end of the supplied input buffer. If clear, the input buffer contains all remaining input. */
/* TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF: If set, the output buffer is large enough to hold the entire decompressed stream. If clear, the output buffer is at least the size of the dictionary (typically 32KB). */
/* TINFL_FLAG_COMPUTE_ADLER32: Force adler-32 checksum computation of the decompressed bytes. */
enum
{
    TINFL_FLAG_PARSE_ZLIB_HEADER = 1,
    TINFL_FLAG_HAS_MORE_INPUT = 2,
    TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF = 4,
    TINFL_FLAG_COMPUTE_ADLER32 = 8
};





struct tinfl_decompressor_tag;
typedef struct tinfl_decompressor_tag tinfl_decompressor;





/* Max size of LZ dictionary. */
#define TINFL_LZ_DICT_SIZE 32768

/* Return status. */
typedef enum {
    /* This flags indicates the inflator needs 1 or more input bytes to make forward progress, but the caller is indicating that no more are available. The compressed data */
    /* is probably corrupted. If you call the inflator again with more bytes it'll try to continue processing the input but this is a BAD sign (either the data is corrupted or you called it incorrectly). */
    /* If you call it again with no input you'll just get TINFL_STATUS_FAILED_CANNOT_MAKE_PROGRESS again. */
    TINFL_STATUS_FAILED_CANNOT_MAKE_PROGRESS = -4,

    /* This flag indicates that one or more of the input parameters was obviously bogus. (You can try calling it again, but if you get this error the calling code is wrong.) */
    TINFL_STATUS_BAD_PARAM = -3,

    /* This flags indicate the inflator is finished but the adler32 check of the uncompressed data didn't match. If you call it again it'll return TINFL_STATUS_DONE. */
    TINFL_STATUS_ADLER32_MISMATCH = -2,

    /* This flags indicate the inflator has somehow failed (bad code, corrupted input, etc.). If you call it again without resetting via tinfl_init() it it'll just keep on returning the same status failure code. */
    TINFL_STATUS_FAILED = -1,

    /* Any status code less than TINFL_STATUS_DONE must indicate a failure. */

    /* This flag indicates the inflator has returned every byte of uncompressed data that it can, has consumed every byte that it needed, has successfully reached the end of the deflate stream, and */
    /* if zlib headers and adler32 checking enabled that it has successfully checked the uncompressed data's adler32. If you call it again you'll just get TINFL_STATUS_DONE over and over again. */
    TINFL_STATUS_DONE = 0,

    /* This flag indicates the inflator MUST have more input data (even 1 byte) before it can make any more forward progress, or you need to clear the TINFL_FLAG_HAS_MORE_INPUT */
    /* flag on the next call if you don't have any more source data. If the source data was somehow corrupted it's also possible (but unlikely) for the inflator to keep on demanding input to */
    /* proceed, so be sure to properly set the TINFL_FLAG_HAS_MORE_INPUT flag. */
    TINFL_STATUS_NEEDS_MORE_INPUT = 1,

    /* This flag indicates the inflator definitely has 1 or more bytes of uncompressed data available, but it cannot write this data into the output buffer. */
    /* Note if the source compressed data was corrupted it's possible for the inflator to return a lot of uncompressed data to the caller. I've been assuming you know how much uncompressed data to expect */
    /* (either exact or worst case) and will stop calling the inflator and fail after receiving too much. In pure streaming scenarios where you have no idea how many bytes to expect this may not be possible */
    /* so I may need to add some code to address this. */
    TINFL_STATUS_HAS_MORE_OUTPUT = 2
} tinfl_status;

/* Initializes the decompressor to its initial state. */
#define tinfl_init(r)     \
    do                    \
    {                     \
        (r)->m_state = 0; \
    }                     \
    while(0)
#define tinfl_get_adler32(r) (r)->m_check_adler32

/* Main low-level decompressor coroutine function. This is the only function actually needed for decompression. All the other functions are just high-level helpers for improved usability. */
/* This is a universal API, i.e. it can be used as a building block to build any desired higher level decompression API. In the limit case, it can be called once per every byte input or output. */
static tinfl_status tinfl_decompress(tinfl_decompressor *r, const uint8_t *pIn_buf_next, size_t *pIn_buf_size, uint8_t *pOut_buf_start, uint8_t *pOut_buf_next, size_t *pOut_buf_size, const uint32_t decomp_flags);

/* Internal/private bits follow. */
enum
{
    TINFL_MAX_HUFF_TABLES = 3,
    TINFL_MAX_HUFF_SYMBOLS_0 = 288,
    TINFL_MAX_HUFF_SYMBOLS_1 = 32,
    TINFL_MAX_HUFF_SYMBOLS_2 = 19,
    TINFL_FAST_LOOKUP_BITS = 10,
    TINFL_FAST_LOOKUP_SIZE = 1 << TINFL_FAST_LOOKUP_BITS
};

typedef struct
{
    uint8_t m_code_size[TINFL_MAX_HUFF_SYMBOLS_0];
    int16_t m_look_up[TINFL_FAST_LOOKUP_SIZE], m_tree[TINFL_MAX_HUFF_SYMBOLS_0 * 2];
} tinfl_huff_table;

#define TINFL_USE_64BIT_BITBUF 0

#if TINFL_USE_64BIT_BITBUF
typedef uint64_t tinfl_bit_buf_t;
#define TINFL_BITBUF_SIZE (64)
#else
typedef uint32_t tinfl_bit_buf_t;
#define TINFL_BITBUF_SIZE (32)
#endif

struct tinfl_decompressor_tag
{
    uint32_t m_state, m_num_bits, m_zhdr0, m_zhdr1, m_z_adler32, m_final, m_type, m_check_adler32, m_dist, m_counter, m_num_extra, m_table_sizes[TINFL_MAX_HUFF_TABLES];
    tinfl_bit_buf_t m_bit_buf;
    size_t m_dist_from_out_buf_start;
    tinfl_huff_table m_tables[TINFL_MAX_HUFF_TABLES];
    uint8_t m_raw_header[4], m_len_codes[TINFL_MAX_HUFF_SYMBOLS_0 + TINFL_MAX_HUFF_SYMBOLS_1 + 137];
};









// topaz addition: import of macros from miniz
#define TINFL_MIN(a, b) (((a) < (b)) ? (a) : (b))
#define TINFL_MAX(a, b) (((a) > (b)) ? (a) : (b))
#define TINFL_CLEAR_OBJ(obj) memset(&(obj), 0, sizeof(obj))
#define TINFL_READ_LE16(p) ((uint32_t)(((const uint8_t *)(p))[0]) | ((uint32_t)(((const uint8_t *)(p))[1]) << 8U))
#define TINFL_READ_LE32(p) ((uint32_t)(((const uint8_t *)(p))[0]) | ((uint32_t)(((const uint8_t *)(p))[1]) << 8U) | ((uint32_t)(((const uint8_t *)(p))[2]) << 16U) | ((uint32_t)(((const uint8_t *)(p))[3]) << 24U))
#define TINFL_READ_LE64(p) (((uint64_t)TINFL_READ_LE32(p)) | (((uint64_t)TINFL_READ_LE32((const uint8_t *)(p) + sizeof(uint32_t))) << 32U))




/* ------------------- Low-level Decompression (completely independent from all compression API's) */

#define TINFL_MEMCPY(d, s, l) memcpy(d, s, l)
#define TINFL_MEMSET(p, c, l) memset(p, c, l)

#define TINFL_CR_BEGIN  \
    switch (r->m_state) \
    {                   \
        case 0:
#define TINFL_CR_RETURN(state_index, result) \
    do                                       \
    {                                        \
        status = result;                     \
        r->m_state = state_index;            \
        goto common_exit;                    \
        case state_index:;                   \
    }                                        \
    while(0)
#define TINFL_CR_RETURN_FOREVER(state_index, result) \
    do                                               \
    {                                                \
        for (;;)                                     \
        {                                            \
            TINFL_CR_RETURN(state_index, result);    \
        }                                            \
    }                                                \
    while(0)
#define TINFL_CR_FINISH }

#define TINFL_GET_BYTE(state_index, c)                                                                                                                           \
    do                                                                                                                                                           \
    {                                                                                                                                                            \
        while (pIn_buf_cur >= pIn_buf_end)                                                                                                                       \
        {                                                                                                                                                        \
            TINFL_CR_RETURN(state_index, (decomp_flags & TINFL_FLAG_HAS_MORE_INPUT) ? TINFL_STATUS_NEEDS_MORE_INPUT : TINFL_STATUS_FAILED_CANNOT_MAKE_PROGRESS); \
        }                                                                                                                                                        \
        c = *pIn_buf_cur++;                                                                                                                                      \
    }                                                                                                                                                            \
    while(0)

#define TINFL_NEED_BITS(state_index, n)                \
    do                                                 \
    {                                                  \
        uint32_t c;                                     \
        TINFL_GET_BYTE(state_index, c);                \
        bit_buf |= (((tinfl_bit_buf_t)c) << num_bits); \
        num_bits += 8;                                 \
    } while (num_bits < (uint32_t)(n))
#define TINFL_SKIP_BITS(state_index, n)      \
    do                                       \
    {                                        \
        if (num_bits < (uint32_t)(n))         \
        {                                    \
            TINFL_NEED_BITS(state_index, n); \
        }                                    \
        bit_buf >>= (n);                     \
        num_bits -= (n);                     \
    }                                        \
    while(0)
#define TINFL_GET_BITS(state_index, b, n)    \
    do                                       \
    {                                        \
        if (num_bits < (uint32_t)(n))         \
        {                                    \
            TINFL_NEED_BITS(state_index, n); \
        }                                    \
        b = bit_buf & ((1 << (n)) - 1);      \
        bit_buf >>= (n);                     \
        num_bits -= (n);                     \
    }                                        \
    while(0)

/* TINFL_HUFF_BITBUF_FILL() is only used rarely, when the number of bytes remaining in the input buffer falls below 2. */
/* It reads just enough bytes from the input stream that are needed to decode the next Huffman code (and absolutely no more). It works by trying to fully decode a */
/* Huffman code by using whatever bits are currently present in the bit buffer. If this fails, it reads another byte, and tries again until it succeeds or until the */
/* bit buffer contains >=15 bits (deflate's max. Huffman code size). */
#define TINFL_HUFF_BITBUF_FILL(state_index, pHuff)                             \
    do                                                                         \
    {                                                                          \
        temp = (pHuff)->m_look_up[bit_buf & (TINFL_FAST_LOOKUP_SIZE - 1)];     \
        if (temp >= 0)                                                         \
        {                                                                      \
            code_len = temp >> 9;                                              \
            if ((code_len) && (num_bits >= code_len))                          \
                break;                                                         \
        }                                                                      \
        else if (num_bits > TINFL_FAST_LOOKUP_BITS)                            \
        {                                                                      \
            code_len = TINFL_FAST_LOOKUP_BITS;                                 \
            do                                                                 \
            {                                                                  \
                temp = (pHuff)->m_tree[~temp + ((bit_buf >> code_len++) & 1)]; \
            } while ((temp < 0) && (num_bits >= (code_len + 1)));              \
            if (temp >= 0)                                                     \
                break;                                                         \
        }                                                                      \
        TINFL_GET_BYTE(state_index, c);                                        \
        bit_buf |= (((tinfl_bit_buf_t)c) << num_bits);                         \
        num_bits += 8;                                                         \
    } while (num_bits < 15);

/* TINFL_HUFF_DECODE() decodes the next Huffman coded symbol. It's more complex than you would initially expect because the zlib API expects the decompressor to never read */
/* beyond the final byte of the deflate stream. (In other words, when this macro wants to read another byte from the input, it REALLY needs another byte in order to fully */
/* decode the next Huffman code.) Handling this properly is particularly important on raw deflate (non-zlib) streams, which aren't followed by a byte aligned adler-32. */
/* The slow path is only executed at the very end of the input buffer. */
/* v1.16: The original macro handled the case at the very end of the passed-in input buffer, but we also need to handle the case where the user passes in 1+zillion bytes */
/* following the deflate data and our non-conservative read-ahead path won't kick in here on this code. This is much trickier. */
#define TINFL_HUFF_DECODE(state_index, sym, pHuff)                                                                                  \
    do                                                                                                                              \
    {                                                                                                                               \
        int temp;                                                                                                                   \
        uint32_t code_len, c;                                                                                                        \
        if (num_bits < 15)                                                                                                          \
        {                                                                                                                           \
            if ((pIn_buf_end - pIn_buf_cur) < 2)                                                                                    \
            {                                                                                                                       \
                TINFL_HUFF_BITBUF_FILL(state_index, pHuff);                                                                         \
            }                                                                                                                       \
            else                                                                                                                    \
            {                                                                                                                       \
                bit_buf |= (((tinfl_bit_buf_t)pIn_buf_cur[0]) << num_bits) | (((tinfl_bit_buf_t)pIn_buf_cur[1]) << (num_bits + 8)); \
                pIn_buf_cur += 2;                                                                                                   \
                num_bits += 16;                                                                                                     \
            }                                                                                                                       \
        }                                                                                                                           \
        if ((temp = (pHuff)->m_look_up[bit_buf & (TINFL_FAST_LOOKUP_SIZE - 1)]) >= 0)                                               \
            code_len = temp >> 9, temp &= 511;                                                                                      \
        else                                                                                                                        \
        {                                                                                                                           \
            code_len = TINFL_FAST_LOOKUP_BITS;                                                                                      \
            do                                                                                                                      \
            {                                                                                                                       \
                temp = (pHuff)->m_tree[~temp + ((bit_buf >> code_len++) & 1)];                                                      \
            } while (temp < 0);                                                                                                     \
        }                                                                                                                           \
        sym = temp;                                                                                                                 \
        bit_buf >>= code_len;                                                                                                       \
        num_bits -= code_len;                                                                                                       \
    }                                                                                                                               \
    while(0)

tinfl_status tinfl_decompress(tinfl_decompressor *r, const uint8_t *pIn_buf_next, size_t *pIn_buf_size, uint8_t *pOut_buf_start, uint8_t *pOut_buf_next, size_t *pOut_buf_size, const uint32_t decomp_flags)
{
    static const int s_length_base[31] = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258, 0, 0 };
    static const int s_length_extra[31] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0, 0, 0 };
    static const int s_dist_base[32] = { 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577, 0, 0 };
    static const int s_dist_extra[32] = { 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13 };
    static const uint8_t s_length_dezigzag[19] = { 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };
    static const int s_min_table_sizes[3] = { 257, 1, 4 };

    tinfl_status status = TINFL_STATUS_FAILED;
    uint32_t num_bits, dist, counter, num_extra;
    tinfl_bit_buf_t bit_buf;
    const uint8_t *pIn_buf_cur = pIn_buf_next, *const pIn_buf_end = pIn_buf_next + *pIn_buf_size;
    uint8_t *pOut_buf_cur = pOut_buf_next, *const pOut_buf_end = pOut_buf_next + *pOut_buf_size;
    size_t out_buf_size_mask = (decomp_flags & TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF) ? (size_t)-1 : ((pOut_buf_next - pOut_buf_start) + *pOut_buf_size) - 1, dist_from_out_buf_start;

    /* Ensure the output buffer's size is a power of 2, unless the output buffer is large enough to hold the entire output file (in which case it doesn't matter). */
    if (((out_buf_size_mask + 1) & out_buf_size_mask) || (pOut_buf_next < pOut_buf_start))
    {
        *pIn_buf_size = *pOut_buf_size = 0;
        return TINFL_STATUS_BAD_PARAM;
    }

    num_bits = r->m_num_bits;
    bit_buf = r->m_bit_buf;
    dist = r->m_dist;
    counter = r->m_counter;
    num_extra = r->m_num_extra;
    dist_from_out_buf_start = r->m_dist_from_out_buf_start;
    TINFL_CR_BEGIN

    bit_buf = num_bits = dist = counter = num_extra = r->m_zhdr0 = r->m_zhdr1 = 0;
    r->m_z_adler32 = r->m_check_adler32 = 1;
    if (decomp_flags & TINFL_FLAG_PARSE_ZLIB_HEADER)
    {
        TINFL_GET_BYTE(1, r->m_zhdr0);
        TINFL_GET_BYTE(2, r->m_zhdr1);
        counter = (((r->m_zhdr0 * 256 + r->m_zhdr1) % 31 != 0) || (r->m_zhdr1 & 32) || ((r->m_zhdr0 & 15) != 8));
        if (!(decomp_flags & TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF))
            counter |= (((1U << (8U + (r->m_zhdr0 >> 4))) > 32768U) || ((out_buf_size_mask + 1) < (size_t)(1U << (8U + (r->m_zhdr0 >> 4)))));
        if (counter)
        {
            TINFL_CR_RETURN_FOREVER(36, TINFL_STATUS_FAILED);
        }
    }

    do
    {
        TINFL_GET_BITS(3, r->m_final, 3);
        r->m_type = r->m_final >> 1;
        if (r->m_type == 0)
        {
            TINFL_SKIP_BITS(5, num_bits & 7);
            for (counter = 0; counter < 4; ++counter)
            {
                if (num_bits)
                    TINFL_GET_BITS(6, r->m_raw_header[counter], 8);
                else
                    TINFL_GET_BYTE(7, r->m_raw_header[counter]);
            }
            if ((counter = (r->m_raw_header[0] | (r->m_raw_header[1] << 8))) != (uint32_t)(0xFFFF ^ (r->m_raw_header[2] | (r->m_raw_header[3] << 8))))
            {
                TINFL_CR_RETURN_FOREVER(39, TINFL_STATUS_FAILED);
            }
            while ((counter) && (num_bits))
            {
                TINFL_GET_BITS(51, dist, 8);
                while (pOut_buf_cur >= pOut_buf_end)
                {
                    TINFL_CR_RETURN(52, TINFL_STATUS_HAS_MORE_OUTPUT);
                }
                *pOut_buf_cur++ = (uint8_t)dist;
                counter--;
            }
            while (counter)
            {
                size_t n;
                while (pOut_buf_cur >= pOut_buf_end)
                {
                    TINFL_CR_RETURN(9, TINFL_STATUS_HAS_MORE_OUTPUT);
                }
                while (pIn_buf_cur >= pIn_buf_end)
                {
                    TINFL_CR_RETURN(38, (decomp_flags & TINFL_FLAG_HAS_MORE_INPUT) ? TINFL_STATUS_NEEDS_MORE_INPUT : TINFL_STATUS_FAILED_CANNOT_MAKE_PROGRESS);
                }
                n = TINFL_MIN(TINFL_MIN((size_t)(pOut_buf_end - pOut_buf_cur), (size_t)(pIn_buf_end - pIn_buf_cur)), counter);
                TINFL_MEMCPY(pOut_buf_cur, pIn_buf_cur, n);
                pIn_buf_cur += n;
                pOut_buf_cur += n;
                counter -= (uint32_t)n;
            }
        }
        else if (r->m_type == 3)
        {
            TINFL_CR_RETURN_FOREVER(10, TINFL_STATUS_FAILED);
        }
        else
        {
            if (r->m_type == 1)
            {
                uint8_t *p = r->m_tables[0].m_code_size;
                uint32_t i;
                r->m_table_sizes[0] = 288;
                r->m_table_sizes[1] = 32;
                TINFL_MEMSET(r->m_tables[1].m_code_size, 5, 32);
                for (i = 0; i <= 143; ++i)
                    *p++ = 8;
                for (; i <= 255; ++i)
                    *p++ = 9;
                for (; i <= 279; ++i)
                    *p++ = 7;
                for (; i <= 287; ++i)
                    *p++ = 8;
            }
            else
            {
                for (counter = 0; counter < 3; counter++)
                {
                    TINFL_GET_BITS(11, r->m_table_sizes[counter], "\05\05\04"[counter]);
                    r->m_table_sizes[counter] += s_min_table_sizes[counter];
                }
                TINFL_CLEAR_OBJ(r->m_tables[2].m_code_size);
                for (counter = 0; counter < r->m_table_sizes[2]; counter++)
                {
                    uint32_t s;
                    TINFL_GET_BITS(14, s, 3);
                    r->m_tables[2].m_code_size[s_length_dezigzag[counter]] = (uint8_t)s;
                }
                r->m_table_sizes[2] = 19;
            }
            for (; (int)r->m_type >= 0; r->m_type--)
            {
                int tree_next, tree_cur;
                tinfl_huff_table *pTable;
                uint32_t i, j, used_syms, total, sym_index, next_code[17], total_syms[16];
                pTable = &r->m_tables[r->m_type];
                TINFL_CLEAR_OBJ(total_syms);
                TINFL_CLEAR_OBJ(pTable->m_look_up);
                TINFL_CLEAR_OBJ(pTable->m_tree);
                for (i = 0; i < r->m_table_sizes[r->m_type]; ++i)
                    total_syms[pTable->m_code_size[i]]++;
                used_syms = 0, total = 0;
                next_code[0] = next_code[1] = 0;
                for (i = 1; i <= 15; ++i)
                {
                    used_syms += total_syms[i];
                    next_code[i + 1] = (total = ((total + total_syms[i]) << 1));
                }
                if ((65536 != total) && (used_syms > 1))
                {
                    TINFL_CR_RETURN_FOREVER(35, TINFL_STATUS_FAILED);
                }
                for (tree_next = -1, sym_index = 0; sym_index < r->m_table_sizes[r->m_type]; ++sym_index)
                {
                    uint32_t rev_code = 0, l, cur_code, code_size = pTable->m_code_size[sym_index];
                    if (!code_size)
                        continue;
                    cur_code = next_code[code_size]++;
                    for (l = code_size; l > 0; l--, cur_code >>= 1)
                        rev_code = (rev_code << 1) | (cur_code & 1);
                    if (code_size <= TINFL_FAST_LOOKUP_BITS)
                    {
                        int16_t k = (int16_t)((code_size << 9) | sym_index);
                        while (rev_code < TINFL_FAST_LOOKUP_SIZE)
                        {
                            pTable->m_look_up[rev_code] = k;
                            rev_code += (1 << code_size);
                        }
                        continue;
                    }
                    if (0 == (tree_cur = pTable->m_look_up[rev_code & (TINFL_FAST_LOOKUP_SIZE - 1)]))
                    {
                        pTable->m_look_up[rev_code & (TINFL_FAST_LOOKUP_SIZE - 1)] = (int16_t)tree_next;
                        tree_cur = tree_next;
                        tree_next -= 2;
                    }
                    rev_code >>= (TINFL_FAST_LOOKUP_BITS - 1);
                    for (j = code_size; j > (TINFL_FAST_LOOKUP_BITS + 1); j--)
                    {
                        tree_cur -= ((rev_code >>= 1) & 1);
                        if (!pTable->m_tree[-tree_cur - 1])
                        {
                            pTable->m_tree[-tree_cur - 1] = (int16_t)tree_next;
                            tree_cur = tree_next;
                            tree_next -= 2;
                        }
                        else
                            tree_cur = pTable->m_tree[-tree_cur - 1];
                    }
                    tree_cur -= ((rev_code >>= 1) & 1);
                    pTable->m_tree[-tree_cur - 1] = (int16_t)sym_index;
                }
                if (r->m_type == 2)
                {
                    for (counter = 0; counter < (r->m_table_sizes[0] + r->m_table_sizes[1]);)
                    {
                        uint32_t s;
                        TINFL_HUFF_DECODE(16, dist, &r->m_tables[2]);
                        if (dist < 16)
                        {
                            r->m_len_codes[counter++] = (uint8_t)dist;
                            continue;
                        }
                        if ((dist == 16) && (!counter))
                        {
                            TINFL_CR_RETURN_FOREVER(17, TINFL_STATUS_FAILED);
                        }
                        num_extra = "\02\03\07"[dist - 16];
                        TINFL_GET_BITS(18, s, num_extra);
                        s += "\03\03\013"[dist - 16];
                        TINFL_MEMSET(r->m_len_codes + counter, (dist == 16) ? r->m_len_codes[counter - 1] : 0, s);
                        counter += s;
                    }
                    if ((r->m_table_sizes[0] + r->m_table_sizes[1]) != counter)
                    {
                        TINFL_CR_RETURN_FOREVER(21, TINFL_STATUS_FAILED);
                    }
                    TINFL_MEMCPY(r->m_tables[0].m_code_size, r->m_len_codes, r->m_table_sizes[0]);
                    TINFL_MEMCPY(r->m_tables[1].m_code_size, r->m_len_codes + r->m_table_sizes[0], r->m_table_sizes[1]);
                }
            }
            for (;;)
            {
                uint8_t *pSrc;
                for (;;)
                {
                    if (((pIn_buf_end - pIn_buf_cur) < 4) || ((pOut_buf_end - pOut_buf_cur) < 2))
                    {
                        TINFL_HUFF_DECODE(23, counter, &r->m_tables[0]);
                        if (counter >= 256)
                            break;
                        while (pOut_buf_cur >= pOut_buf_end)
                        {
                            TINFL_CR_RETURN(24, TINFL_STATUS_HAS_MORE_OUTPUT);
                        }
                        *pOut_buf_cur++ = (uint8_t)counter;
                    }
                    else
                    {
                        int sym2;
                        uint32_t code_len;
#if TINFL_USE_64BIT_BITBUF
                        if (num_bits < 30)
                        {
                            bit_buf |= (((tinfl_bit_buf_t)TINFL_READ_LE32(pIn_buf_cur)) << num_bits);
                            pIn_buf_cur += 4;
                            num_bits += 32;
                        }
#else
                        if (num_bits < 15)
                        {
                            bit_buf |= (((tinfl_bit_buf_t)TINFL_READ_LE16(pIn_buf_cur)) << num_bits);
                            pIn_buf_cur += 2;
                            num_bits += 16;
                        }
#endif
                        if ((sym2 = r->m_tables[0].m_look_up[bit_buf & (TINFL_FAST_LOOKUP_SIZE - 1)]) >= 0)
                            code_len = sym2 >> 9;
                        else
                        {
                            code_len = TINFL_FAST_LOOKUP_BITS;
                            do
                            {
                                sym2 = r->m_tables[0].m_tree[~sym2 + ((bit_buf >> code_len++) & 1)];
                            } while (sym2 < 0);
                        }
                        counter = sym2;
                        bit_buf >>= code_len;
                        num_bits -= code_len;
                        if (counter & 256)
                            break;

#if !TINFL_USE_64BIT_BITBUF
                        if (num_bits < 15)
                        {
                            bit_buf |= (((tinfl_bit_buf_t)TINFL_READ_LE16(pIn_buf_cur)) << num_bits);
                            pIn_buf_cur += 2;
                            num_bits += 16;
                        }
#endif
                        if ((sym2 = r->m_tables[0].m_look_up[bit_buf & (TINFL_FAST_LOOKUP_SIZE - 1)]) >= 0)
                            code_len = sym2 >> 9;
                        else
                        {
                            code_len = TINFL_FAST_LOOKUP_BITS;
                            do
                            {
                                sym2 = r->m_tables[0].m_tree[~sym2 + ((bit_buf >> code_len++) & 1)];
                            } while (sym2 < 0);
                        }
                        bit_buf >>= code_len;
                        num_bits -= code_len;

                        pOut_buf_cur[0] = (uint8_t)counter;
                        if (sym2 & 256)
                        {
                            pOut_buf_cur++;
                            counter = sym2;
                            break;
                        }
                        pOut_buf_cur[1] = (uint8_t)sym2;
                        pOut_buf_cur += 2;
                    }
                }
                if ((counter &= 511) == 256)
                    break;

                num_extra = s_length_extra[counter - 257];
                counter = s_length_base[counter - 257];
                if (num_extra)
                {
                    uint32_t extra_bits;
                    TINFL_GET_BITS(25, extra_bits, num_extra);
                    counter += extra_bits;
                }

                TINFL_HUFF_DECODE(26, dist, &r->m_tables[1]);
                num_extra = s_dist_extra[dist];
                dist = s_dist_base[dist];
                if (num_extra)
                {
                    uint32_t extra_bits;
                    TINFL_GET_BITS(27, extra_bits, num_extra);
                    dist += extra_bits;
                }

                dist_from_out_buf_start = pOut_buf_cur - pOut_buf_start;
                if ((dist == 0 || dist > dist_from_out_buf_start || dist_from_out_buf_start == 0) && (decomp_flags & TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF))
                {
                    TINFL_CR_RETURN_FOREVER(37, TINFL_STATUS_FAILED);
                }

                pSrc = pOut_buf_start + ((dist_from_out_buf_start - dist) & out_buf_size_mask);

                if ((TINFL_MAX(pOut_buf_cur, pSrc) + counter) > pOut_buf_end)
                {
                    while (counter--)
                    {
                        while (pOut_buf_cur >= pOut_buf_end)
                        {
                            TINFL_CR_RETURN(53, TINFL_STATUS_HAS_MORE_OUTPUT);
                        }
                        *pOut_buf_cur++ = pOut_buf_start[(dist_from_out_buf_start++ - dist) & out_buf_size_mask];
                    }
                    continue;
                }
#if MINIZ_USE_UNALIGNED_LOADS_AND_STORES
                else if ((counter >= 9) && (counter <= dist))
                {
                    const uint8_t *pSrc_end = pSrc + (counter & ~7);
                    do
                    {
#ifdef MINIZ_UNALIGNED_USE_MEMCPY
						memcpy(pOut_buf_cur, pSrc, sizeof(uint32_t)*2);
#else
                        ((uint32_t *)pOut_buf_cur)[0] = ((const uint32_t *)pSrc)[0];
                        ((uint32_t *)pOut_buf_cur)[1] = ((const uint32_t *)pSrc)[1];
#endif
                        pOut_buf_cur += 8;
                    } while ((pSrc += 8) < pSrc_end);
                    if ((counter &= 7) < 3)
                    {
                        if (counter)
                        {
                            pOut_buf_cur[0] = pSrc[0];
                            if (counter > 1)
                                pOut_buf_cur[1] = pSrc[1];
                            pOut_buf_cur += counter;
                        }
                        continue;
                    }
                }
#endif
                while(counter>2)
                {
                    pOut_buf_cur[0] = pSrc[0];
                    pOut_buf_cur[1] = pSrc[1];
                    pOut_buf_cur[2] = pSrc[2];
                    pOut_buf_cur += 3;
                    pSrc += 3;
					counter -= 3;
                }
                if (counter > 0)
                {
                    pOut_buf_cur[0] = pSrc[0];
                    if (counter > 1)
                        pOut_buf_cur[1] = pSrc[1];
                    pOut_buf_cur += counter;
                }
            }
        }
    } while (!(r->m_final & 1));

    /* Ensure byte alignment and put back any bytes from the bitbuf if we've looked ahead too far on gzip, or other Deflate streams followed by arbitrary data. */
    /* I'm being super conservative here. A number of simplifications can be made to the byte alignment part, and the Adler32 check shouldn't ever need to worry about reading from the bitbuf now. */
    TINFL_SKIP_BITS(32, num_bits & 7);
    while ((pIn_buf_cur > pIn_buf_next) && (num_bits >= 8))
    {
        --pIn_buf_cur;
        num_bits -= 8;
    }
    bit_buf &= (tinfl_bit_buf_t)((((uint64_t)1) << num_bits) - (uint64_t)1);
    //assert(!num_bits); /* if this assert fires then we've read beyond the end of non-deflate/zlib streams with following data (such as gzip streams). */

    if (decomp_flags & TINFL_FLAG_PARSE_ZLIB_HEADER)
    {
        for (counter = 0; counter < 4; ++counter)
        {
            uint32_t s;
            if (num_bits)
                TINFL_GET_BITS(41, s, 8);
            else
                TINFL_GET_BYTE(42, s);
            r->m_z_adler32 = (r->m_z_adler32 << 8) | s;
        }
    }
    TINFL_CR_RETURN_FOREVER(34, TINFL_STATUS_DONE);

    TINFL_CR_FINISH

common_exit:
    /* As long as we aren't telling the caller that we NEED more input to make forward progress: */
    /* Put back any bytes from the bitbuf in case we've looked ahead too far on gzip, or other Deflate streams followed by arbitrary data. */
    /* We need to be very careful here to NOT push back any bytes we definitely know we need to make forward progress, though, or we'll lock the caller up into an inf loop. */
    if ((status != TINFL_STATUS_NEEDS_MORE_INPUT) && (status != TINFL_STATUS_FAILED_CANNOT_MAKE_PROGRESS))
    {
        while ((pIn_buf_cur > pIn_buf_next) && (num_bits >= 8))
        {
            --pIn_buf_cur;
            num_bits -= 8;
        }
    }
    r->m_num_bits = num_bits;
    r->m_bit_buf = bit_buf & (tinfl_bit_buf_t)((((uint64_t)1) << num_bits) - (uint64_t)1);
    r->m_dist = dist;
    r->m_counter = counter;
    r->m_num_extra = num_extra;
    r->m_dist_from_out_buf_start = dist_from_out_buf_start;
    *pIn_buf_size = pIn_buf_cur - pIn_buf_next;
    *pOut_buf_size = pOut_buf_cur - pOut_buf_next;
    if ((decomp_flags & (TINFL_FLAG_PARSE_ZLIB_HEADER | TINFL_FLAG_COMPUTE_ADLER32)) && (status >= 0))
    {
        const uint8_t *ptr = pOut_buf_next;
        size_t buf_len = *pOut_buf_size;
        uint32_t i, s1 = r->m_check_adler32 & 0xffff, s2 = r->m_check_adler32 >> 16;
        size_t block_len = buf_len % 5552;
        while (buf_len)
        {
            for (i = 0; i + 7 < block_len; i += 8, ptr += 8)
            {
                s1 += ptr[0], s2 += s1;
                s1 += ptr[1], s2 += s1;
                s1 += ptr[2], s2 += s1;
                s1 += ptr[3], s2 += s1;
                s1 += ptr[4], s2 += s1;
                s1 += ptr[5], s2 += s1;
                s1 += ptr[6], s2 += s1;
                s1 += ptr[7], s2 += s1;
            }
            for (; i < block_len; ++i)
                s1 += *ptr++, s2 += s1;
            s1 %= 65521U, s2 %= 65521U;
            buf_len -= block_len;
            block_len = 5552;
        }
        r->m_check_adler32 = (s2 << 16) + s1;
        if ((status == TINFL_STATUS_DONE) && (decomp_flags & TINFL_FLAG_PARSE_ZLIB_HEADER) && (r->m_check_adler32 != r->m_z_adler32))
            status = TINFL_STATUS_ADLER32_MISMATCH;
    }
    return status;
}

/* High level decompression functions: */
/* tinfl_decompress_mem_to_heap() decompresses a block in memory to a heap block allocated via TPNG_MALLOC(). */
/* On entry: */
/*  pSrc_buf, src_buf_len: Pointer and size of the Deflate or zlib source data to decompress. */
/* On return: */
/*  Function returns a pointer to the decompressed data, or NULL on failure. */
/*  *pOut_len will be set to the decompressed data's size, which could be larger than src_buf_len on uncompressible data. */
/*  The caller must call mz_TPNG_FREE() on the returned block when it's no longer needed. */

void *tinfl_decompress_mem_to_heap(const void *pSrc_buf, size_t src_buf_len, size_t *pOut_len, int flags)
{
    tinfl_decompressor decomp;
    void *pBuf = NULL, *pNew_buf;
    size_t src_buf_ofs = 0, out_buf_capacity = 0;
    *pOut_len = 0;
    tinfl_init(&decomp);
    for (;;)
    {
        size_t src_buf_size = src_buf_len - src_buf_ofs, dst_buf_size = out_buf_capacity - *pOut_len, new_out_buf_capacity;
        tinfl_status status = tinfl_decompress(&decomp, (const uint8_t *)pSrc_buf + src_buf_ofs, &src_buf_size, (uint8_t *)pBuf, pBuf ? (uint8_t *)pBuf + *pOut_len : NULL, &dst_buf_size,
                                               (flags & ~TINFL_FLAG_HAS_MORE_INPUT) | TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);
        if ((status < 0) || (status == TINFL_STATUS_NEEDS_MORE_INPUT))
        {
            TPNG_FREE(pBuf);
            *pOut_len = 0;
            return NULL;
        }
        src_buf_ofs += src_buf_size;
        *pOut_len += dst_buf_size;
        if (status == TINFL_STATUS_DONE)
            break;
        new_out_buf_capacity = out_buf_capacity * 2;
        if (new_out_buf_capacity < 128)
            new_out_buf_capacity = 128;
        pNew_buf = realloc(pBuf, new_out_buf_capacity);
        if (!pNew_buf)
        {
            TPNG_FREE(pBuf);
            *pOut_len = 0;
            return NULL;
        }
        pBuf = pNew_buf;
        out_buf_capacity = new_out_buf_capacity;
    }
    return pBuf;
}





#ifdef __cplusplus
}
#endif





/* Wrapper function for kernel API compatibility */
uint8_t *tpng_decode(const uint8_t *data, uint32_t size, uint32_t *width, uint32_t *height) {
    return tpng_get_rgba(data, size, width, height);
}
