/*
 * VibeOS Graphics Library
 *
 * Common drawing primitives for GUI applications.
 * Works with any buffer - desktop backbuffer, window buffers, etc.
 */

#ifndef GFX_H
#define GFX_H

#include "vibe.h"

// Graphics context - describes a drawing target
typedef struct {
    uint32_t *buffer;      // Pixel buffer
    int width;             // Buffer width in pixels
    int height;            // Buffer height in pixels
    const uint8_t *font;   // Font data (from kapi->font_data)
} gfx_ctx_t;

// Initialize a graphics context
static inline void gfx_init(gfx_ctx_t *ctx, uint32_t *buffer, int w, int h, const uint8_t *font) {
    ctx->buffer = buffer;
    ctx->width = w;
    ctx->height = h;
    ctx->font = font;
}

// ============ Basic Drawing Primitives ============

// Put a single pixel
static inline void gfx_put_pixel(gfx_ctx_t *ctx, int x, int y, uint32_t color) {
    if (x >= 0 && x < ctx->width && y >= 0 && y < ctx->height) {
        ctx->buffer[y * ctx->width + x] = color;
    }
}

// Fill a rectangle with solid color (optimized with 64-bit stores)
static inline void gfx_fill_rect(gfx_ctx_t *ctx, int x, int y, int w, int h, uint32_t color) {
    // Clip to bounds
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > ctx->width) w = ctx->width - x;
    if (y + h > ctx->height) h = ctx->height - y;
    if (w <= 0 || h <= 0) return;

    // Fill row by row using fast 64-bit memset
    for (int py = y; py < y + h; py++) {
        memset32_fast(&ctx->buffer[py * ctx->width + x], color, w);
    }
}

// Draw a horizontal line (optimized with 64-bit stores)
static inline void gfx_draw_hline(gfx_ctx_t *ctx, int x, int y, int w, uint32_t color) {
    if (y < 0 || y >= ctx->height) return;
    // Clip to bounds
    if (x < 0) { w += x; x = 0; }
    if (x + w > ctx->width) w = ctx->width - x;
    if (w <= 0) return;
    memset32_fast(&ctx->buffer[y * ctx->width + x], color, w);
}

// Draw a vertical line
static inline void gfx_draw_vline(gfx_ctx_t *ctx, int x, int y, int h, uint32_t color) {
    if (x < 0 || x >= ctx->width) return;
    for (int i = 0; i < h; i++) {
        int py = y + i;
        if (py >= 0 && py < ctx->height) {
            ctx->buffer[py * ctx->width + x] = color;
        }
    }
}

// Draw a rectangle outline
static inline void gfx_draw_rect(gfx_ctx_t *ctx, int x, int y, int w, int h, uint32_t color) {
    gfx_draw_hline(ctx, x, y, w, color);
    gfx_draw_hline(ctx, x, y + h - 1, w, color);
    gfx_draw_vline(ctx, x, y, h, color);
    gfx_draw_vline(ctx, x + w - 1, y, h, color);
}

// ============ Text Drawing ============

// Draw a single character (8x16 font)
static inline void gfx_draw_char(gfx_ctx_t *ctx, int x, int y, char c, uint32_t fg, uint32_t bg) {
    const uint8_t *glyph = &ctx->font[(unsigned char)c * 16];
    for (int row = 0; row < 16; row++) {
        for (int col = 0; col < 8; col++) {
            uint32_t color = (glyph[row] & (0x80 >> col)) ? fg : bg;
            int px = x + col;
            int py = y + row;
            if (px >= 0 && px < ctx->width && py >= 0 && py < ctx->height) {
                ctx->buffer[py * ctx->width + px] = color;
            }
        }
    }
}

// Draw a string
static inline void gfx_draw_string(gfx_ctx_t *ctx, int x, int y, const char *s, uint32_t fg, uint32_t bg) {
    while (*s) {
        gfx_draw_char(ctx, x, y, *s, fg, bg);
        x += 8;
        s++;
    }
}

// Draw a string with clipping (max width in pixels)
static inline void gfx_draw_string_clip(gfx_ctx_t *ctx, int x, int y, const char *s, uint32_t fg, uint32_t bg, int max_w) {
    int drawn = 0;
    while (*s && drawn + 8 <= max_w) {
        gfx_draw_char(ctx, x, y, *s, fg, bg);
        x += 8;
        drawn += 8;
        s++;
    }
}

// ============ TTF Text Drawing ============

// Draw a TTF glyph (grayscale antialiased)
// The glyph bitmap is grayscale 0-255, we blend with background
static inline void gfx_draw_ttf_glyph(gfx_ctx_t *ctx, int x, int y, ttf_glyph_t *glyph, uint32_t fg, uint32_t bg) {
    if (!glyph || !glyph->bitmap) return;

    // Apply glyph offsets
    x += glyph->xoff;
    y += glyph->yoff;

    uint8_t fg_r = (fg >> 16) & 0xFF;
    uint8_t fg_g = (fg >> 8) & 0xFF;
    uint8_t fg_b = fg & 0xFF;
    uint8_t bg_r = (bg >> 16) & 0xFF;
    uint8_t bg_g = (bg >> 8) & 0xFF;
    uint8_t bg_b = bg & 0xFF;

    for (int row = 0; row < glyph->height; row++) {
        for (int col = 0; col < glyph->width; col++) {
            int px = x + col;
            int py = y + row;
            if (px < 0 || px >= ctx->width || py < 0 || py >= ctx->height) continue;

            uint8_t alpha = glyph->bitmap[row * glyph->width + col];
            if (alpha == 0) continue;  // Fully transparent

            if (alpha == 255) {
                // Fully opaque
                ctx->buffer[py * ctx->width + px] = fg;
            } else {
                // Blend
                uint8_t r = (fg_r * alpha + bg_r * (255 - alpha)) / 255;
                uint8_t g = (fg_g * alpha + bg_g * (255 - alpha)) / 255;
                uint8_t b = (fg_b * alpha + bg_b * (255 - alpha)) / 255;
                ctx->buffer[py * ctx->width + px] = (r << 16) | (g << 8) | b;
            }
        }
    }
}

// Draw a TTF string at given size and style
// Returns the width of the drawn string in pixels
static inline int gfx_draw_ttf_string(gfx_ctx_t *ctx, kapi_t *k, int x, int y,
                                       const char *s, int size, int style,
                                       uint32_t fg, uint32_t bg) {
    if (!k->ttf_is_ready || !k->ttf_is_ready()) {
        // Fallback to bitmap font
        gfx_draw_string(ctx, x, y, s, fg, bg);
        return strlen(s) * 8;
    }

    // Get font metrics for baseline
    int ascent, descent, line_gap;
    k->ttf_get_metrics(size, &ascent, &descent, &line_gap);

    int start_x = x;
    int prev_cp = 0;

    while (*s) {
        int cp = (unsigned char)*s;

        // Add kerning
        if (prev_cp) {
            x += k->ttf_get_kerning(prev_cp, cp, size);
        }

        ttf_glyph_t *glyph = (ttf_glyph_t *)k->ttf_get_glyph(cp, size, style);
        if (glyph) {
            // y + ascent puts the baseline at y + ascent
            // glyph->yoff is relative to baseline (negative = above)
            gfx_draw_ttf_glyph(ctx, x, y + ascent, glyph, fg, bg);
            x += glyph->advance;
        } else {
            x += size / 2;  // Default advance for missing glyph
        }

        prev_cp = cp;
        s++;
    }

    return x - start_x;
}

// ============ Patterns (for desktop background, etc.) ============

// Classic Mac diagonal checkerboard pattern (optimized with 64-bit stores)
static inline void gfx_fill_pattern(gfx_ctx_t *ctx, int x, int y, int w, int h, uint32_t c1, uint32_t c2) {
    // Clip to bounds
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > ctx->width) w = ctx->width - x;
    if (y + h > ctx->height) h = ctx->height - y;
    if (w <= 0 || h <= 0) return;

    // Precompute 64-bit patterns for two pixels at a time
    // Pattern alternates c1,c2 or c2,c1 depending on row parity
    uint64_t pattern_even = ((uint64_t)c2 << 32) | c1;  // c1 at even x, c2 at odd x
    uint64_t pattern_odd = ((uint64_t)c1 << 32) | c2;   // c2 at even x, c1 at odd x

    for (int py = y; py < y + h; py++) {
        uint32_t *row = &ctx->buffer[py * ctx->width + x];
        int row_parity = (py + x) & 1;  // Determines starting color

        // Use 64-bit stores for pairs of pixels
        if ((x & 1) == 0 && w >= 2) {
            uint64_t *row64 = (uint64_t *)row;
            uint64_t p = row_parity ? pattern_odd : pattern_even;
            int pairs = w / 2;
            for (int i = 0; i < pairs; i++) {
                row64[i] = p;
            }
            // Handle odd pixel at end
            if (w & 1) {
                row[w - 1] = ((py + x + w - 1) & 1) ? c2 : c1;
            }
        } else {
            // Fallback for unaligned start
            for (int px = 0; px < w; px++) {
                row[px] = ((px + x + py) & 1) ? c2 : c1;
            }
        }
    }
}

// 25% dither pattern (sparse dots)
static inline void gfx_fill_dither25(gfx_ctx_t *ctx, int x, int y, int w, int h, uint32_t c1, uint32_t c2) {
    for (int py = y; py < y + h && py < ctx->height; py++) {
        if (py < 0) continue;
        for (int px = x; px < x + w && px < ctx->width; px++) {
            if (px < 0) continue;
            int pattern = ((px % 2 == 0) && (py % 2 == 0)) ? 1 : 0;
            ctx->buffer[py * ctx->width + px] = pattern ? c1 : c2;
        }
    }
}

// ============ Alpha Blending ============

// Extract RGB components
#define GFX_R(c) (((c) >> 16) & 0xFF)
#define GFX_G(c) (((c) >> 8) & 0xFF)
#define GFX_B(c) ((c) & 0xFF)
#define GFX_RGB(r, g, b) (((r) << 16) | ((g) << 8) | (b))

// Blend two colors: result = src * alpha + dst * (255 - alpha)
// alpha is 0-255
static inline uint32_t gfx_blend(uint32_t src, uint32_t dst, uint8_t alpha) {
    if (alpha == 255) return src;
    if (alpha == 0) return dst;

    uint32_t sr = GFX_R(src), sg = GFX_G(src), sb = GFX_B(src);
    uint32_t dr = GFX_R(dst), dg = GFX_G(dst), db = GFX_B(dst);
    uint32_t inv = 255 - alpha;

    uint32_t r = (sr * alpha + dr * inv) / 255;
    uint32_t g = (sg * alpha + dg * inv) / 255;
    uint32_t b = (sb * alpha + db * inv) / 255;

    return GFX_RGB(r, g, b);
}

// Put a pixel with alpha blending
static inline void gfx_put_pixel_alpha(gfx_ctx_t *ctx, int x, int y, uint32_t color, uint8_t alpha) {
    if (x < 0 || x >= ctx->width || y < 0 || y >= ctx->height) return;
    uint32_t dst = ctx->buffer[y * ctx->width + x];
    ctx->buffer[y * ctx->width + x] = gfx_blend(color, dst, alpha);
}

// Fill rectangle with alpha blending
static inline void gfx_fill_rect_alpha(gfx_ctx_t *ctx, int x, int y, int w, int h, uint32_t color, uint8_t alpha) {
    // Clip to bounds
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > ctx->width) w = ctx->width - x;
    if (y + h > ctx->height) h = ctx->height - y;
    if (w <= 0 || h <= 0) return;

    for (int py = y; py < y + h; py++) {
        for (int px = x; px < x + w; px++) {
            uint32_t dst = ctx->buffer[py * ctx->width + px];
            ctx->buffer[py * ctx->width + px] = gfx_blend(color, dst, alpha);
        }
    }
}

// ============ Gradients ============

// Interpolate between two colors (t is 0-255)
static inline uint32_t gfx_lerp_color(uint32_t c1, uint32_t c2, uint8_t t) {
    uint32_t r = (GFX_R(c1) * (255 - t) + GFX_R(c2) * t) / 255;
    uint32_t g = (GFX_G(c1) * (255 - t) + GFX_G(c2) * t) / 255;
    uint32_t b = (GFX_B(c1) * (255 - t) + GFX_B(c2) * t) / 255;
    return GFX_RGB(r, g, b);
}

// Vertical gradient (top to bottom)
static inline void gfx_gradient_v(gfx_ctx_t *ctx, int x, int y, int w, int h, uint32_t top, uint32_t bottom) {
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > ctx->width) w = ctx->width - x;
    if (y + h > ctx->height) h = ctx->height - y;
    if (w <= 0 || h <= 0) return;

    for (int py = 0; py < h; py++) {
        uint8_t t = (py * 255) / (h > 1 ? h - 1 : 1);
        uint32_t color = gfx_lerp_color(top, bottom, t);
        memset32_fast(&ctx->buffer[(y + py) * ctx->width + x], color, w);
    }
}

// Horizontal gradient (left to right)
static inline void gfx_gradient_h(gfx_ctx_t *ctx, int x, int y, int w, int h, uint32_t left, uint32_t right) {
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > ctx->width) w = ctx->width - x;
    if (y + h > ctx->height) h = ctx->height - y;
    if (w <= 0 || h <= 0) return;

    for (int py = 0; py < h; py++) {
        uint32_t *row = &ctx->buffer[(y + py) * ctx->width + x];
        for (int px = 0; px < w; px++) {
            uint8_t t = (px * 255) / (w > 1 ? w - 1 : 1);
            row[px] = gfx_lerp_color(left, right, t);
        }
    }
}

// Vertical gradient with alpha
static inline void gfx_gradient_v_alpha(gfx_ctx_t *ctx, int x, int y, int w, int h,
                                         uint32_t top, uint32_t bottom, uint8_t alpha) {
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > ctx->width) w = ctx->width - x;
    if (y + h > ctx->height) h = ctx->height - y;
    if (w <= 0 || h <= 0) return;

    for (int py = 0; py < h; py++) {
        uint8_t t = (py * 255) / (h > 1 ? h - 1 : 1);
        uint32_t color = gfx_lerp_color(top, bottom, t);
        for (int px = 0; px < w; px++) {
            int sx = x + px, sy = y + py;
            uint32_t dst = ctx->buffer[sy * ctx->width + sx];
            ctx->buffer[sy * ctx->width + sx] = gfx_blend(color, dst, alpha);
        }
    }
}

// ============ Rounded Rectangles ============

// Fill a rounded rectangle
static inline void gfx_fill_rounded_rect(gfx_ctx_t *ctx, int x, int y, int w, int h, int r, uint32_t color) {
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;
    if (r < 1) { gfx_fill_rect(ctx, x, y, w, h, color); return; }

    // Fill the main rectangles (cross shape)
    gfx_fill_rect(ctx, x + r, y, w - 2 * r, h, color);      // Middle vertical
    gfx_fill_rect(ctx, x, y + r, r, h - 2 * r, color);      // Left side
    gfx_fill_rect(ctx, x + w - r, y + r, r, h - 2 * r, color); // Right side

    // Fill corners using circle algorithm
    int r2 = r * r;
    for (int cy = 0; cy < r; cy++) {
        for (int cx = 0; cx < r; cx++) {
            int dx = r - 1 - cx;
            int dy = r - 1 - cy;
            if (dx * dx + dy * dy <= r2) {
                // Top-left
                gfx_put_pixel(ctx, x + cx, y + cy, color);
                // Top-right
                gfx_put_pixel(ctx, x + w - 1 - cx, y + cy, color);
                // Bottom-left
                gfx_put_pixel(ctx, x + cx, y + h - 1 - cy, color);
                // Bottom-right
                gfx_put_pixel(ctx, x + w - 1 - cx, y + h - 1 - cy, color);
            }
        }
    }
}

// Fill rounded rect with alpha
static inline void gfx_fill_rounded_rect_alpha(gfx_ctx_t *ctx, int x, int y, int w, int h,
                                                int r, uint32_t color, uint8_t alpha) {
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;
    if (r < 1) { gfx_fill_rect_alpha(ctx, x, y, w, h, color, alpha); return; }

    // Fill the main rectangles (cross shape)
    gfx_fill_rect_alpha(ctx, x + r, y, w - 2 * r, h, color, alpha);
    gfx_fill_rect_alpha(ctx, x, y + r, r, h - 2 * r, color, alpha);
    gfx_fill_rect_alpha(ctx, x + w - r, y + r, r, h - 2 * r, color, alpha);

    // Fill corners
    int r2 = r * r;
    for (int cy = 0; cy < r; cy++) {
        for (int cx = 0; cx < r; cx++) {
            int dx = r - 1 - cx;
            int dy = r - 1 - cy;
            if (dx * dx + dy * dy <= r2) {
                gfx_put_pixel_alpha(ctx, x + cx, y + cy, color, alpha);
                gfx_put_pixel_alpha(ctx, x + w - 1 - cx, y + cy, color, alpha);
                gfx_put_pixel_alpha(ctx, x + cx, y + h - 1 - cy, color, alpha);
                gfx_put_pixel_alpha(ctx, x + w - 1 - cx, y + h - 1 - cy, color, alpha);
            }
        }
    }
}

// Draw rounded rectangle outline
static inline void gfx_draw_rounded_rect(gfx_ctx_t *ctx, int x, int y, int w, int h, int r, uint32_t color) {
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;
    if (r < 1) { gfx_draw_rect(ctx, x, y, w, h, color); return; }

    // Draw straight edges
    gfx_draw_hline(ctx, x + r, y, w - 2 * r, color);           // Top
    gfx_draw_hline(ctx, x + r, y + h - 1, w - 2 * r, color);   // Bottom
    gfx_draw_vline(ctx, x, y + r, h - 2 * r, color);           // Left
    gfx_draw_vline(ctx, x + w - 1, y + r, h - 2 * r, color);   // Right

    // Draw corner arcs using midpoint circle algorithm
    int cx = r - 1, cy = 0;
    int d = 1 - r;

    while (cx >= cy) {
        // Top-left corner
        gfx_put_pixel(ctx, x + r - 1 - cx, y + r - 1 - cy, color);
        gfx_put_pixel(ctx, x + r - 1 - cy, y + r - 1 - cx, color);
        // Top-right corner
        gfx_put_pixel(ctx, x + w - r + cx, y + r - 1 - cy, color);
        gfx_put_pixel(ctx, x + w - r + cy, y + r - 1 - cx, color);
        // Bottom-left corner
        gfx_put_pixel(ctx, x + r - 1 - cx, y + h - r + cy, color);
        gfx_put_pixel(ctx, x + r - 1 - cy, y + h - r + cx, color);
        // Bottom-right corner
        gfx_put_pixel(ctx, x + w - r + cx, y + h - r + cy, color);
        gfx_put_pixel(ctx, x + w - r + cy, y + h - r + cx, color);

        cy++;
        if (d < 0) {
            d += 2 * cy + 1;
        } else {
            cx--;
            d += 2 * (cy - cx) + 1;
        }
    }
}

// ============ Box Shadow (approximated blur) ============

// Draw a soft shadow behind a rectangle
// Uses multiple translucent rectangles to approximate gaussian blur
static inline void gfx_box_shadow(gfx_ctx_t *ctx, int x, int y, int w, int h,
                                   int blur, int offset_x, int offset_y, uint32_t color) {
    if (blur <= 0) {
        gfx_fill_rect_alpha(ctx, x + offset_x, y + offset_y, w, h, color, 128);
        return;
    }

    // Draw multiple layers with decreasing opacity
    for (int i = blur; i >= 0; i--) {
        int expand = blur - i;
        uint8_t alpha = (255 * (blur - i + 1)) / (blur * 4);  // Fade out
        gfx_fill_rect_alpha(ctx, x + offset_x - expand, y + offset_y - expand,
                            w + expand * 2, h + expand * 2, color, alpha);
    }
}

// Rounded box shadow
static inline void gfx_box_shadow_rounded(gfx_ctx_t *ctx, int x, int y, int w, int h, int r,
                                           int blur, int offset_x, int offset_y, uint32_t color) {
    if (blur <= 0) {
        gfx_fill_rounded_rect_alpha(ctx, x + offset_x, y + offset_y, w, h, r, color, 128);
        return;
    }

    for (int i = blur; i >= 0; i--) {
        int expand = blur - i;
        int nr = r + expand;  // Grow corner radius too
        uint8_t alpha = (255 * (blur - i + 1)) / (blur * 4);
        gfx_fill_rounded_rect_alpha(ctx, x + offset_x - expand, y + offset_y - expand,
                                     w + expand * 2, h + expand * 2, nr, color, alpha);
    }
}

// ============ Simple Box Blur ============

// Box blur a region of the buffer (for frosted glass effect)
// This is EXPENSIVE - would need GPU for real-time
static inline void gfx_blur_region(gfx_ctx_t *ctx, int x, int y, int w, int h, int radius) {
    if (radius <= 0) return;

    // Clip
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > ctx->width) w = ctx->width - x;
    if (y + h > ctx->height) h = ctx->height - y;
    if (w <= 0 || h <= 0) return;

    // Allocate temp buffer (on stack for small regions, or skip if too large)
    // For a real implementation, we'd need a separate buffer
    // This is a simplified single-pass box blur

    int kernel_size = radius * 2 + 1;
    int divisor = kernel_size * kernel_size;

    // Process each pixel
    for (int py = y; py < y + h; py++) {
        for (int px = x; px < x + w; px++) {
            uint32_t sum_r = 0, sum_g = 0, sum_b = 0;
            int count = 0;

            // Sample kernel
            for (int ky = -radius; ky <= radius; ky++) {
                for (int kx = -radius; kx <= radius; kx++) {
                    int sx = px + kx, sy = py + ky;
                    if (sx >= 0 && sx < ctx->width && sy >= 0 && sy < ctx->height) {
                        uint32_t c = ctx->buffer[sy * ctx->width + sx];
                        sum_r += GFX_R(c);
                        sum_g += GFX_G(c);
                        sum_b += GFX_B(c);
                        count++;
                    }
                }
            }

            if (count > 0) {
                ctx->buffer[py * ctx->width + px] = GFX_RGB(sum_r / count, sum_g / count, sum_b / count);
            }
        }
    }
}

#endif // GFX_H
