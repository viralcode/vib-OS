/*
 * Vib-OS - MJPEG AVI builder (for small embedded demos)
 */

#include "media/avi_build.h"
#include "mm/kmalloc.h"
#include "printk.h"

static inline void wr_u16le(uint8_t *buf, size_t *off, uint16_t v) {
  buf[(*off)++] = (uint8_t)(v & 0xFF);
  buf[(*off)++] = (uint8_t)((v >> 8) & 0xFF);
}

static inline void wr_u32le(uint8_t *buf, size_t *off, uint32_t v) {
  buf[(*off)++] = (uint8_t)(v & 0xFF);
  buf[(*off)++] = (uint8_t)((v >> 8) & 0xFF);
  buf[(*off)++] = (uint8_t)((v >> 16) & 0xFF);
  buf[(*off)++] = (uint8_t)((v >> 24) & 0xFF);
}

static inline void wr_fourcc(uint8_t *buf, size_t *off, const char *cc) {
  buf[(*off)++] = (uint8_t)cc[0];
  buf[(*off)++] = (uint8_t)cc[1];
  buf[(*off)++] = (uint8_t)cc[2];
  buf[(*off)++] = (uint8_t)cc[3];
}

static inline void wr_bytes(uint8_t *buf, size_t *off, const void *src,
                            size_t len) {
  const uint8_t *p = (const uint8_t *)src;
  for (size_t i = 0; i < len; i++) {
    buf[(*off)++] = p[i];
  }
}

static inline size_t pad2(size_t n) { return (n + 1) & ~1u; }

int media_build_mjpeg_avi(uint8_t **out_data, size_t *out_size,
                          const uint8_t **frames,
                          const uint32_t *frame_sizes,
                          uint32_t frame_count, uint32_t width,
                          uint32_t height, uint32_t fps) {
  if (!out_data || !out_size || !frames || !frame_sizes || frame_count == 0 ||
      fps == 0) {
    return -EINVAL;
  }

  uint32_t max_frame = 0;
  uint64_t total_frame_bytes = 0;
  for (uint32_t i = 0; i < frame_count; i++) {
    uint32_t sz = frame_sizes[i];
    if (sz > max_frame)
      max_frame = sz;
    total_frame_bytes += (uint64_t)pad2((size_t)sz);
  }

  uint32_t us_per_frame = 1000000u / fps;
  uint32_t avg_bytes_per_sec =
      (uint32_t)((total_frame_bytes * (uint64_t)fps) /
                 (frame_count ? frame_count : 1));

  /* LIST strl size (payload size, excluding 'LIST'+size) */
  const uint32_t strh_chunk = 8 + 56;
  const uint32_t strf_chunk = 8 + 40;
  const uint32_t strl_list_payload = 4 + strh_chunk + strf_chunk;

  /* LIST hdrl contains avih + LIST strl */
  const uint32_t avih_chunk = 8 + 56;
  const uint32_t hdrl_list_payload = 4 + avih_chunk + (8 + strl_list_payload);

  /* LIST movi payload: 'movi' + frame chunks */
  uint64_t movi_chunks = 4;
  for (uint32_t i = 0; i < frame_count; i++) {
    movi_chunks += 8 + (uint64_t)pad2((size_t)frame_sizes[i]);
  }
  if (movi_chunks > 0xFFFFFFFFu) {
    printk(KERN_ERR "AVI_BUILD: movi too large\n");
    return -EFBIG;
  }
  uint32_t movi_list_payload = (uint32_t)movi_chunks;

  /* idx1 chunk */
  uint64_t idx1_sz64 = (uint64_t)frame_count * 16ull;
  if (idx1_sz64 > 0xFFFFFFFFu) {
    printk(KERN_ERR "AVI_BUILD: idx1 too large\n");
    return -EFBIG;
  }
  uint32_t idx1_payload = (uint32_t)idx1_sz64;

  /* Total RIFF payload size (excluding 'RIFF'+size field) */
  uint64_t riff_payload =
      4ull + /* 'AVI ' */
      (8ull + (uint64_t)hdrl_list_payload) +
      (8ull + (uint64_t)movi_list_payload) + (8ull + (uint64_t)idx1_payload);

  if (riff_payload + 8ull > 0xFFFFFFFFu) {
    printk(KERN_ERR "AVI_BUILD: AVI too large\n");
    return -EFBIG;
  }

  size_t file_size = (size_t)riff_payload + 8;
  uint8_t *buf = (uint8_t *)kmalloc(file_size, GFP_KERNEL);
  if (!buf)
    return -ENOMEM;

  uint32_t *chunk_offsets =
      (uint32_t *)kmalloc(frame_count * sizeof(uint32_t), GFP_KERNEL);
  if (!chunk_offsets) {
    kfree(buf);
    return -ENOMEM;
  }

  size_t off = 0;

  /* RIFF header */
  wr_fourcc(buf, &off, "RIFF");
  wr_u32le(buf, &off, (uint32_t)(file_size - 8));
  wr_fourcc(buf, &off, "AVI ");

  /* LIST hdrl */
  wr_fourcc(buf, &off, "LIST");
  wr_u32le(buf, &off, hdrl_list_payload);
  wr_fourcc(buf, &off, "hdrl");

  /* avih */
  wr_fourcc(buf, &off, "avih");
  wr_u32le(buf, &off, 56);
  wr_u32le(buf, &off, us_per_frame);
  wr_u32le(buf, &off, avg_bytes_per_sec);
  wr_u32le(buf, &off, 0);      /* dwPaddingGranularity */
  wr_u32le(buf, &off, 0x10u);  /* dwFlags = AVIF_HASINDEX */
  wr_u32le(buf, &off, frame_count);
  wr_u32le(buf, &off, 0); /* dwInitialFrames */
  wr_u32le(buf, &off, 1); /* dwStreams */
  wr_u32le(buf, &off, max_frame);
  wr_u32le(buf, &off, width);
  wr_u32le(buf, &off, height);
  wr_u32le(buf, &off, 0);
  wr_u32le(buf, &off, 0);
  wr_u32le(buf, &off, 0);
  wr_u32le(buf, &off, 0);

  /* LIST strl */
  wr_fourcc(buf, &off, "LIST");
  wr_u32le(buf, &off, strl_list_payload);
  wr_fourcc(buf, &off, "strl");

  /* strh */
  wr_fourcc(buf, &off, "strh");
  wr_u32le(buf, &off, 56);
  wr_fourcc(buf, &off, "vids");
  wr_fourcc(buf, &off, "MJPG");
  wr_u32le(buf, &off, 0); /* flags */
  wr_u16le(buf, &off, 0); /* priority */
  wr_u16le(buf, &off, 0); /* language */
  wr_u32le(buf, &off, 0); /* initial frames */
  wr_u32le(buf, &off, 1); /* scale */
  wr_u32le(buf, &off, fps);
  wr_u32le(buf, &off, 0);          /* start */
  wr_u32le(buf, &off, frame_count); /* length */
  wr_u32le(buf, &off, max_frame);
  wr_u32le(buf, &off, 0xFFFFFFFFu); /* quality */
  wr_u32le(buf, &off, 0);           /* sample size */
  /* rcFrame (left, top, right, bottom) as 16-bit */
  wr_u16le(buf, &off, 0);
  wr_u16le(buf, &off, 0);
  wr_u16le(buf, &off, (uint16_t)(width & 0xFFFF));
  wr_u16le(buf, &off, (uint16_t)(height & 0xFFFF));

  /* strf (BITMAPINFOHEADER) */
  wr_fourcc(buf, &off, "strf");
  wr_u32le(buf, &off, 40);
  wr_u32le(buf, &off, 40); /* biSize */
  wr_u32le(buf, &off, width);
  wr_u32le(buf, &off, height);
  wr_u16le(buf, &off, 1);  /* planes */
  wr_u16le(buf, &off, 24); /* bit count */
  wr_fourcc(buf, &off, "MJPG");
  wr_u32le(buf, &off, 0); /* size image */
  wr_u32le(buf, &off, 0); /* x ppm */
  wr_u32le(buf, &off, 0); /* y ppm */
  wr_u32le(buf, &off, 0); /* clr used */
  wr_u32le(buf, &off, 0); /* clr important */

  /* LIST movi */
  wr_fourcc(buf, &off, "LIST");
  wr_u32le(buf, &off, movi_list_payload);
  wr_fourcc(buf, &off, "movi");

  size_t movi_data_off = off; /* base for idx1 offsets (first chunk header) */
  (void)movi_data_off;

  for (uint32_t i = 0; i < frame_count; i++) {
    chunk_offsets[i] = (uint32_t)(off - movi_data_off);
    wr_fourcc(buf, &off, "00dc");
    wr_u32le(buf, &off, frame_sizes[i]);
    wr_bytes(buf, &off, frames[i], frame_sizes[i]);
    if (frame_sizes[i] & 1) {
      buf[off++] = 0;
    }
  }

  /* idx1 */
  wr_fourcc(buf, &off, "idx1");
  wr_u32le(buf, &off, idx1_payload);

  for (uint32_t i = 0; i < frame_count; i++) {
    wr_fourcc(buf, &off, "00dc");
    wr_u32le(buf, &off, 0x10u); /* keyframe */
    wr_u32le(buf, &off, chunk_offsets[i]);
    wr_u32le(buf, &off, frame_sizes[i]);
  }

  kfree(chunk_offsets);

  if (off != file_size) {
    printk(KERN_WARNING "AVI_BUILD: size mismatch (%zu vs %zu)\n", off,
           file_size);
  }

  *out_data = buf;
  *out_size = file_size;
  return 0;
}

