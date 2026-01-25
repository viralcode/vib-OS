/*
 * doomgeneric for VibeOS
 * Platform-specific implementation for doomgeneric port
 *
 * Copyright (C) 2024-2025 Kaan Senol
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 2.
 */

#include "doom_libc.h"
#include "doomgeneric.h"
#include "doomkeys.h"
#include "d_event.h"

/* External function to post events to DOOM */
extern void D_PostEvent(event_t *ev);

/* Global kapi pointer - also used by doom_libc */
kapi_t *doom_kapi = 0;

/* Start time for DG_GetTicksMs */
static uint64_t start_ticks = 0;

/* Screen positioning - calculated at runtime to center on any resolution */
static int screen_offset_x = 0;
static int screen_offset_y = 0;
static int scale_factor = 1;

/* Key queue for input */
#define KEYQUEUE_SIZE 64
static struct {
    unsigned char key;
    int pressed;
} key_queue[KEYQUEUE_SIZE];
static int key_queue_read = 0;
static int key_queue_write = 0;

/* Track which keys are currently held (for release events) */
static unsigned char keys_held[256];

/* Add a key event to the queue */
static void add_key_event(unsigned char doom_key, int pressed) {
    int next = (key_queue_write + 1) % KEYQUEUE_SIZE;
    if (next != key_queue_read) {
        key_queue[key_queue_write].key = doom_key;
        key_queue[key_queue_write].pressed = pressed;
        key_queue_write = next;
    }
}

/* Map VibeOS key to DOOM key */
static unsigned char translate_key(int vibe_key) {
    /* Arrow keys */
    if (vibe_key == 0x100) return KEY_UPARROW;     /* KEY_UP */
    if (vibe_key == 0x101) return KEY_DOWNARROW;   /* KEY_DOWN */
    if (vibe_key == 0x102) return KEY_LEFTARROW;   /* KEY_LEFT */
    if (vibe_key == 0x103) return KEY_RIGHTARROW;  /* KEY_RIGHT */

    /* Modifier keys */
    if (vibe_key == 0x109) return KEY_RCTRL;       /* SPECIAL_KEY_CTRL = fire */
    if (vibe_key == 0x10A) return KEY_RSHIFT;      /* SPECIAL_KEY_SHIFT = run */

    /* Special keys */
    if (vibe_key == 27) return KEY_ESCAPE;
    if (vibe_key == '\n' || vibe_key == '\r') return KEY_ENTER;
    if (vibe_key == '\t') return KEY_TAB;
    if (vibe_key == ' ') return KEY_USE;           /* Space = use/open doors */
    if (vibe_key == 127 || vibe_key == 8) return KEY_BACKSPACE;

    /* Control key (ASCII 1-26 are Ctrl+letter) - also fire */
    if (vibe_key >= 1 && vibe_key <= 26) {
        return KEY_RCTRL;  /* Ctrl+letter = fire */
    }

    /* Function keys (if supported) */
    if (vibe_key >= 0x110 && vibe_key <= 0x11B) {
        return KEY_F1 + (vibe_key - 0x110);
    }

    /* WASD movement + E for use */
    if (vibe_key == 'w' || vibe_key == 'W') return KEY_UPARROW;
    if (vibe_key == 's' || vibe_key == 'S') return KEY_DOWNARROW;
    if (vibe_key == 'a' || vibe_key == 'A') return KEY_STRAFE_L;
    if (vibe_key == 'd' || vibe_key == 'D') return KEY_STRAFE_R;
    if (vibe_key == 'e' || vibe_key == 'E') return KEY_USE;

    /* Letters - lowercase them for DOOM */
    if (vibe_key >= 'A' && vibe_key <= 'Z') {
        return vibe_key + 32;  /* lowercase */
    }
    if (vibe_key >= 'a' && vibe_key <= 'z') {
        return vibe_key;
    }

    /* Numbers and common symbols */
    if (vibe_key >= '0' && vibe_key <= '9') return vibe_key;
    if (vibe_key == '-') return KEY_MINUS;
    if (vibe_key == '=') return KEY_EQUALS;
    if (vibe_key == '+') return '+';
    if (vibe_key == ',') return ',';
    if (vibe_key == '.') return '.';
    if (vibe_key == '/') return '/';

    /* Y/N for prompts */
    if (vibe_key == 'y' || vibe_key == 'Y') return 'y';
    if (vibe_key == 'n' || vibe_key == 'N') return 'n';

    return 0;  /* Unknown key */
}

/* Poll keyboard and queue events */
static void poll_keys(void) {
    /* Drive the input system */
    if (doom_kapi->input_poll) {
        doom_kapi->input_poll();
    }

    while (doom_kapi->has_key()) {
        int c = doom_kapi->getc();
        if (c < 0) break;

        unsigned char doom_key = translate_key(c);
        if (doom_key) {
            /* Key press */
            add_key_event(doom_key, 1);
            keys_held[doom_key] = 1;
        }
    }

    /* Generate release events for held keys after a delay
     * (VibeOS doesn't have key-up events, so we fake them) */
    static uint64_t last_release_check = 0;
    uint64_t now = doom_kapi->get_uptime_ticks();
    if (now - last_release_check > 10) {  /* Every 100ms */
        last_release_check = now;
        for (int i = 0; i < 256; i++) {
            if (keys_held[i]) {
                add_key_event(i, 0);  /* Release */
                keys_held[i] = 0;
            }
        }
    }
}

/* Poll mouse and post events to DOOM */
static void poll_mouse(void) {
    if (!doom_kapi->mouse_get_delta) return;

    /* Get accumulated delta (this also polls and clears) */
    int dx, dy;
    doom_kapi->mouse_get_delta(&dx, &dy);

    /* Get button state */
    uint8_t buttons = doom_kapi->mouse_get_buttons();
    int doom_buttons = 0;
    if (buttons & 0x01) doom_buttons |= 1;  /* Left = fire */
    if (buttons & 0x02) doom_buttons |= 2;  /* Right */
    if (buttons & 0x04) doom_buttons |= 4;  /* Middle */

    /* Post event if there's movement or buttons pressed */
    if (dx != 0 || doom_buttons) {
        event_t ev;
        ev.type = ev_mouse;
        ev.data1 = doom_buttons;
        ev.data2 = dx * 2;   /* Scale up for better sensitivity */
        ev.data3 = 0;        /* Ignore Y - mouse for turning only */
        ev.data4 = 0;
        D_PostEvent(&ev);
    }
}

/* ============ DoomGeneric Platform Functions ============ */

void DG_Init(void) {
    /* Record start time */
    start_ticks = doom_kapi->get_uptime_ticks();

    /* Force 2x scaling for fullscreen effect */
    int fb_w = doom_kapi->fb_width;
    int fb_h = doom_kapi->fb_height;
    
    /* Use 2x scaling for fuller screen coverage */
    scale_factor = 2;
    
    /* Calculate centering offsets - may be negative for overscan */
    int scaled_w = DOOMGENERIC_RESX * scale_factor;  /* 1280 */
    int scaled_h = DOOMGENERIC_RESY * scale_factor;  /* 800 */
    
    /* Center on screen - negative offset means we clip the edges */
    screen_offset_x = (fb_w - scaled_w) / 2;  /* (1024-1280)/2 = -128 */
    screen_offset_y = (fb_h - scaled_h) / 2;  /* (768-800)/2 = -16 */

    /* Clear screen to black */
    if (doom_kapi->fb_base) {
        uint32_t *fb = doom_kapi->fb_base;
        int total = fb_w * fb_h;
        for (int i = 0; i < total; i++) {
            fb[i] = 0;
        }
    }

    /* Initialize key state */
    for (int i = 0; i < 256; i++) {
        keys_held[i] = 0;
    }

    printf("DG_Init: VibeOS DOOM initialized (FULLSCREEN 2x)\n");
    printf("  DOOM res: %dx%d, scale: %dx, screen: %dx%d\n",
           DOOMGENERIC_RESX, DOOMGENERIC_RESY, scale_factor, fb_w, fb_h);
    printf("  Offset: (%d,%d), scaled: %dx%d\n",
           screen_offset_x, screen_offset_y, scaled_w, scaled_h);
}

void DG_DrawFrame(void) {
    if (!doom_kapi->fb_base || !DG_ScreenBuffer) return;

    uint32_t *fb = doom_kapi->fb_base;
    int fb_width = doom_kapi->fb_width;
    int fb_height = doom_kapi->fb_height;

    /* 2x scaling with clipping for overscan */
    for (int y = 0; y < DOOMGENERIC_RESY; y++) {
        pixel_t *src_row = DG_ScreenBuffer + y * DOOMGENERIC_RESX;
        
        for (int sy = 0; sy < scale_factor; sy++) {
            int dest_y = y * scale_factor + sy + screen_offset_y;
            
            /* Skip if row is off-screen (clipping) */
            if (dest_y < 0 || dest_y >= fb_height) continue;
            
            for (int x = 0; x < DOOMGENERIC_RESX; x++) {
                uint32_t pixel = src_row[x];
                
                for (int sx = 0; sx < scale_factor; sx++) {
                    int dest_x = x * scale_factor + sx + screen_offset_x;
                    
                    /* Skip if column is off-screen (clipping) */
                    if (dest_x < 0 || dest_x >= fb_width) continue;
                    
                    fb[dest_y * fb_width + dest_x] = pixel;
                }
            }
        }
    }
}

void DG_SleepMs(uint32_t ms) {
    doom_kapi->sleep_ms(ms);
}

uint32_t DG_GetTicksMs(void) {
    /* VibeOS ticks are 10ms each (100Hz timer) */
    uint64_t now = doom_kapi->get_uptime_ticks();
    return (uint32_t)((now - start_ticks) * 10);
}

int DG_GetKey(int *pressed, unsigned char *doomKey) {
    /* Poll for new input */
    poll_keys();
    poll_mouse();

    /* Return key from queue if available */
    if (key_queue_read != key_queue_write) {
        *pressed = key_queue[key_queue_read].pressed;
        *doomKey = key_queue[key_queue_read].key;
        key_queue_read = (key_queue_read + 1) % KEYQUEUE_SIZE;
        return 1;
    }

    return 0;  /* No key available */
}

void DG_SetWindowTitle(const char *title) {
    /* No window title in VibeOS - just print to console */
    (void)title;
}

/* ============ Main Entry Point ============ */

int main(kapi_t *api, int argc, char **argv) {
    /* Save kapi pointer globally */
    doom_kapi = api;

    /* Initialize libc with kapi */
    doom_libc_init(api);

    /* Clear screen */
    api->clear();

    printf("DOOM for VibeOS\n");
    printf("===============\n\n");

    /* Default arguments if none provided */
    static char *default_argv[] = {
        "doom",
        "-iwad", "/games/doom1.wad",
        NULL
    };

    if (argc < 2) {
        printf("No WAD specified, using default: /games/doom1.wad\n");
        argc = 3;
        argv = default_argv;
    }

    printf("Starting DOOM with %d args:\n", argc);
    for (int i = 0; i < argc; i++) {
        printf("  argv[%d] = %s\n", i, argv[i]);
    }
    printf("\n");

    /* Initialize DOOM */
    printf("Calling doomgeneric_Create...\n");
    doomgeneric_Create(argc, argv);

    printf("Entering main loop...\n");

    /* Main game loop */
    while (1) {
        doomgeneric_Tick();
    }

    return 0;
}

/* ========================================================================= */
/* Sound Interface Implementation */
/* ========================================================================= */

#include "i_sound.h"

void I_InitSound(boolean use_sfx_prefix) {
    (void)use_sfx_prefix;
    if (doom_kapi) {
        /* Assuming stereo 44100Hz for now, though Doom uses 11025Hz 8-bit usually */
        /* Note: proper mixer needed for real audio */
        // doom_kapi->puts("I_InitSound: Initialized\n");
    }
}

void I_ShutdownSound(void) {
}

int I_GetSfxLumpNum(sfxinfo_t *sfxinfo) {
    /* Use default behavior if possible, or stub */
    return 0; 
}

void I_UpdateSound(void) {
    /* Called every tick. Could mix here. */
}

void I_UpdateSoundParams(int channel, int vol, int sep) {
}

/* Rudimentary single-channel playback for testing */
int I_StartSound(sfxinfo_t *sfxinfo, int channel, int vol, int sep) {
    if (!doom_kapi || !sfxinfo) return -1;
    
    /* Just log for now */
    // char buf[64];
    // sprintf(buf, "Play Sound: %s vol=%d\n", sfxinfo->name, vol);
    // doom_kapi->puts(buf);
    
    /* If we had data, we'd send it */
    if (sfxinfo->data) {
        /* Doom sounds are 8-bit mono, usually 11025Hz */
        /* Skip header if present (PCFX) - usually 8 bytes? varies */
        /* For simplicity, send a small chunk to prove connectivity */
        doom_kapi->sound_play_pcm_async(sfxinfo->data, 100, 1, 11025);
    }
    
    return channel;
}

void I_StopSound(int channel) {
}

boolean I_SoundIsPlaying(int channel) {
    return false;
}

void I_PrecacheSounds(sfxinfo_t *sounds, int num_sounds) {
}

/* Music stubs */
void I_InitMusic(void) {}
void I_ShutdownMusic(void) {}
void I_SetMusicVolume(int volume) {}
void I_PauseSong(void) {}
void I_ResumeSong(void) {}
void *I_RegisterSong(void *data, int len) { return (void*)1; }
void I_UnRegisterSong(void *handle) {}
void I_PlaySong(void *handle, boolean looping) {}
void I_StopSong(void) {}
boolean I_MusicIsPlaying(void) { return false; }
void I_BindSoundVariables(void) {}

