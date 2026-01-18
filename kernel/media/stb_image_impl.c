/*
 * STB Image Implementation
 * Single-header image loading library
 * 
 * Supports: JPEG, PNG, BMP, TGA, PSD, GIF, HDR, PIC, PNM
 */

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO  /* We'll provide our own file I/O */
#define STBI_NO_LINEAR /* Don't need HDR/float support */
#define STBI_NO_HDR    /* Don't need HDR support */

/* Provide malloc/free/realloc */
#include "mm/kmalloc.h"
#define STBI_MALLOC(sz)           kmalloc(sz, 0)
#define STBI_REALLOC(p,newsz)     krealloc(p, newsz, 0)
#define STBI_FREE(p)              kfree(p)

/* Provide assert */
#include "printk.h"
#define STBI_ASSERT(x)  do { if (!(x)) { printk(KERN_ERR "STB_IMAGE assertion failed: %s\n", #x); } } while(0)

/* Include the header */
#include "media/stb_image.h"

/* Helper function to load image from memory buffer */
unsigned char* stbi_load_from_memory_wrapper(const unsigned char *buffer, int len, 
                                              int *x, int *y, int *channels_in_file, 
                                              int desired_channels)
{
    return stbi_load_from_memory(buffer, len, x, y, channels_in_file, desired_channels);
}

/* Helper function to free image data */
void stbi_image_free_wrapper(void *data)
{
    stbi_image_free(data);
}

/* Get failure reason */
const char* stbi_failure_reason_wrapper(void)
{
    return stbi_failure_reason();
}
