#ifndef _KERNEL_AVI_H
#define _KERNEL_AVI_H

#include "types.h"

/*
 * Minimal AVI MJPEG (Motion-JPEG) demuxer.
 *
 * Supported:
 * - RIFF AVI
 * - Video stream with handler "MJPG"
 * - Frame indexing via idx1 (preferred) or movi scan (fallback)
 *
 * Not supported (yet):
 * - Audio extraction/playback
 * - ODML/OpenDML extensions
 * - Compressed indexes / super indexes
 */

typedef struct {
  const uint8_t *data;
  size_t size;

  uint32_t width;
  uint32_t height;

  uint32_t total_frames;   /* From avih, if present */
  uint32_t us_per_frame;   /* From avih, if present */

  uint32_t frame_count;    /* Indexed MJPEG frames */
  uint32_t *frame_offsets; /* Absolute offsets to JPEG payload (not chunk hdr) */
  uint32_t *frame_sizes;   /* JPEG payload sizes */
} avi_mjpeg_t;

int avi_mjpeg_open(avi_mjpeg_t *avi, const uint8_t *data, size_t size);
void avi_mjpeg_close(avi_mjpeg_t *avi);

int avi_mjpeg_get_frame(const avi_mjpeg_t *avi, uint32_t index,
                        const uint8_t **out_data, size_t *out_size);

#endif /* _KERNEL_AVI_H */

