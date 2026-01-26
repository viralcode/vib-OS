/*
 * Vib-OS - Minimal AVI MJPEG demuxer
 */

#include "media/avi.h"
#include "mm/kmalloc.h"
#include "printk.h"

static inline uint32_t rd_le32(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
         ((uint32_t)p[3] << 24);
}

static inline int fourcc_eq(const uint8_t *p, const char a, const char b,
                            const char c, const char d) {
  return p[0] == (uint8_t)a && p[1] == (uint8_t)b && p[2] == (uint8_t)c &&
         p[3] == (uint8_t)d;
}

static inline int is_mjpeg_chunk_id(uint32_t id) {
  /* Typical MJPEG chunks are "00dc" or "00db" */
  uint8_t a = (uint8_t)(id & 0xFF);
  uint8_t b = (uint8_t)((id >> 8) & 0xFF);
  uint8_t c = (uint8_t)((id >> 16) & 0xFF);
  uint8_t d = (uint8_t)((id >> 24) & 0xFF);
  if (c != 'd')
    return 0;
  if (d != 'c' && d != 'b')
    return 0;
  (void)a;
  (void)b;
  return 1;
}

static int scan_movi_for_frames(avi_mjpeg_t *avi, size_t movi_data_off,
                                size_t movi_data_size) {
  if (!avi || !avi->data || movi_data_off >= avi->size)
    return -1;

  size_t pos = movi_data_off;
  size_t end = movi_data_off + movi_data_size;
  if (end > avi->size)
    end = avi->size;

  /* First pass: count frames */
  uint32_t count = 0;
  while (pos + 8 <= end) {
    uint32_t id = rd_le32(avi->data + pos);
    uint32_t sz = rd_le32(avi->data + pos + 4);
    pos += 8;
    if (pos + sz > end)
      break;
    if (is_mjpeg_chunk_id(id))
      count++;
    /* Chunks are word-aligned */
    pos += (sz + 1) & ~1u;
  }

  if (count == 0)
    return -1;

  avi->frame_offsets = (uint32_t *)kmalloc(count * sizeof(uint32_t), GFP_KERNEL);
  avi->frame_sizes = (uint32_t *)kmalloc(count * sizeof(uint32_t), GFP_KERNEL);
  if (!avi->frame_offsets || !avi->frame_sizes) {
    if (avi->frame_offsets)
      kfree(avi->frame_offsets);
    if (avi->frame_sizes)
      kfree(avi->frame_sizes);
    avi->frame_offsets = NULL;
    avi->frame_sizes = NULL;
    return -1;
  }

  /* Second pass: fill */
  pos = movi_data_off;
  uint32_t idx = 0;
  while (pos + 8 <= end && idx < count) {
    uint32_t id = rd_le32(avi->data + pos);
    uint32_t sz = rd_le32(avi->data + pos + 4);
    pos += 8;
    if (pos + sz > end)
      break;
    if (is_mjpeg_chunk_id(id)) {
      avi->frame_offsets[idx] = (uint32_t)pos; /* payload start */
      avi->frame_sizes[idx] = sz;
      idx++;
    }
    pos += (sz + 1) & ~1u;
  }

  avi->frame_count = idx;
  return (avi->frame_count > 0) ? 0 : -1;
}

static int parse_idx1_frames(avi_mjpeg_t *avi, size_t idx1_off, size_t idx1_sz,
                             size_t movi_list_type_off,
                             size_t movi_data_off) {
  if (!avi || !avi->data)
    return -1;
  if (idx1_off + 8 > avi->size)
    return -1;

  /* idx1 entries are 16 bytes each */
  uint32_t entry_count = (uint32_t)(idx1_sz / 16);
  if (entry_count == 0)
    return -1;
  if (entry_count > (1024 * 1024))
    return -1;

  /* Decide idx base by probing first few entries */
  size_t base_candidates[2] = {movi_list_type_off, movi_data_off};
  int best_base_idx = 0;
  int best_hits = -1;
  for (int bi = 0; bi < 2; bi++) {
    int hits = 0;
    for (uint32_t i = 0; i < entry_count && i < 64; i++) {
      size_t eoff = idx1_off + i * 16;
      uint32_t id = rd_le32(avi->data + eoff);
      uint32_t off = rd_le32(avi->data + eoff + 8);
      size_t abs = base_candidates[bi] + (size_t)off;
      if (abs + 8 <= avi->size && rd_le32(avi->data + abs) == id)
        hits++;
    }
    if (hits > best_hits) {
      best_hits = hits;
      best_base_idx = bi;
    }
  }
  size_t idx_base = base_candidates[best_base_idx];

  /* Count MJPEG entries */
  uint32_t count = 0;
  for (uint32_t i = 0; i < entry_count; i++) {
    size_t eoff = idx1_off + i * 16;
    uint32_t id = rd_le32(avi->data + eoff);
    if (is_mjpeg_chunk_id(id))
      count++;
  }
  if (count == 0)
    return -1;

  avi->frame_offsets = (uint32_t *)kmalloc(count * sizeof(uint32_t), GFP_KERNEL);
  avi->frame_sizes = (uint32_t *)kmalloc(count * sizeof(uint32_t), GFP_KERNEL);
  if (!avi->frame_offsets || !avi->frame_sizes) {
    if (avi->frame_offsets)
      kfree(avi->frame_offsets);
    if (avi->frame_sizes)
      kfree(avi->frame_sizes);
    avi->frame_offsets = NULL;
    avi->frame_sizes = NULL;
    return -1;
  }

  uint32_t out_i = 0;
  for (uint32_t i = 0; i < entry_count && out_i < count; i++) {
    size_t eoff = idx1_off + i * 16;
    uint32_t id = rd_le32(avi->data + eoff + 0);
    uint32_t sz = rd_le32(avi->data + eoff + 12);
    uint32_t rel = rd_le32(avi->data + eoff + 8);
    if (!is_mjpeg_chunk_id(id))
      continue;

    size_t abs = idx_base + (size_t)rel;
    if (abs + 8 > avi->size)
      continue;

    /* Validate header matches; if not, try alternate base once */
    if (rd_le32(avi->data + abs) != id) {
      size_t alt_base = base_candidates[best_base_idx ^ 1];
      size_t alt_abs = alt_base + (size_t)rel;
      if (alt_abs + 8 <= avi->size && rd_le32(avi->data + alt_abs) == id) {
        abs = alt_abs;
      } else {
        continue;
      }
    }

    /* Payload begins after chunk header */
    size_t payload = abs + 8;
    if (payload + sz > avi->size)
      continue;

    avi->frame_offsets[out_i] = (uint32_t)payload;
    avi->frame_sizes[out_i] = sz;
    out_i++;
  }

  avi->frame_count = out_i;
  return (avi->frame_count > 0) ? 0 : -1;
}

int avi_mjpeg_open(avi_mjpeg_t *avi, const uint8_t *data, size_t size) {
  if (!avi || !data || size < 16)
    return -1;

  avi->data = data;
  avi->size = size;
  avi->width = 0;
  avi->height = 0;
  avi->total_frames = 0;
  avi->us_per_frame = 0;
  avi->frame_count = 0;
  avi->frame_offsets = NULL;
  avi->frame_sizes = NULL;

  if (!fourcc_eq(data + 0, 'R', 'I', 'F', 'F'))
    return -1;
  if (!fourcc_eq(data + 8, 'A', 'V', 'I', ' '))
    return -1;

  size_t movi_list_type_off = 0;
  size_t movi_data_off = 0;
  size_t movi_data_size = 0;
  size_t idx1_off = 0;
  size_t idx1_sz = 0;

  /* Walk RIFF chunks */
  size_t pos = 12;
  while (pos + 8 <= size) {
    uint32_t id = rd_le32(data + pos);
    uint32_t sz = rd_le32(data + pos + 4);
    size_t payload = pos + 8;
    size_t next = payload + ((size_t)sz + 1) & ~1u;
    if (next > size)
      break;

    if (id == rd_le32((const uint8_t *)"LIST") && payload + 4 <= size) {
      uint32_t list_type = rd_le32(data + payload);
      size_t list_data = payload + 4;
      size_t list_sz = (sz >= 4) ? (sz - 4) : 0;

      if (list_type == rd_le32((const uint8_t *)"movi")) {
        movi_list_type_off = payload; /* points at 'movi' */
        movi_data_off = list_data;
        movi_data_size = list_sz;
      }

      /* Parse hdrl for avih/strf */
      if (list_type == rd_le32((const uint8_t *)"hdrl")) {
        size_t p2 = list_data;
        size_t end2 = list_data + list_sz;
        if (end2 > size)
          end2 = size;

        /* Track if we found an MJPG video stream */
        int video_mjpg = 0;
        while (p2 + 8 <= end2) {
          uint32_t cid = rd_le32(data + p2);
          uint32_t csz = rd_le32(data + p2 + 4);
          size_t cp = p2 + 8;
          size_t cn = cp + ((size_t)csz + 1) & ~1u;
          if (cn > end2)
            break;

          if (cid == rd_le32((const uint8_t *)"avih") && csz >= 56) {
            avi->us_per_frame = rd_le32(data + cp + 0);
            avi->total_frames = rd_le32(data + cp + 16);
          } else if (cid == rd_le32((const uint8_t *)"LIST") && csz >= 4 &&
                     cp + 4 <= end2) {
            uint32_t lt = rd_le32(data + cp);
            if (lt == rd_le32((const uint8_t *)"strl")) {
              size_t sp = cp + 4;
              size_t send = cp + (size_t)csz;
              if (send > end2)
                send = end2;

              uint32_t stream_type = 0;
              uint32_t stream_handler = 0;
              uint32_t w = 0, h = 0;

              size_t sp2 = sp;
              while (sp2 + 8 <= send) {
                uint32_t sid = rd_le32(data + sp2);
                uint32_t ssz = rd_le32(data + sp2 + 4);
                size_t spay = sp2 + 8;
                size_t snext = spay + ((size_t)ssz + 1) & ~1u;
                if (snext > send)
                  break;

                if (sid == rd_le32((const uint8_t *)"strh") && ssz >= 56) {
                  stream_type = rd_le32(data + spay + 0);
                  stream_handler = rd_le32(data + spay + 4);
                } else if (sid == rd_le32((const uint8_t *)"strf") &&
                           ssz >= 40) {
                  /* BITMAPINFOHEADER */
                  w = rd_le32(data + spay + 4);
                  h = rd_le32(data + spay + 8);
                }
                sp2 = snext;
              }

              if (stream_type == rd_le32((const uint8_t *)"vids") &&
                  stream_handler == rd_le32((const uint8_t *)"MJPG")) {
                video_mjpg = 1;
                if (w && h) {
                  avi->width = w;
                  avi->height = h;
                }
              }
            }
          }

          p2 = cn;
        }
        if (!video_mjpg) {
          /* Still allow playback if we can index frames, but warn */
          printk(KERN_WARNING "AVI: No MJPG 'vids' stream found in headers\n");
        }
      }
    } else if (id == rd_le32((const uint8_t *)"idx1")) {
      idx1_off = payload;
      idx1_sz = sz;
    }

    pos = next;
  }

  if (movi_data_off == 0 || movi_list_type_off == 0) {
    printk(KERN_ERR "AVI: Missing movi list\n");
    return -1;
  }

  /* Prefer idx1 for seeking */
  int ok = -1;
  if (idx1_off && idx1_sz >= 16) {
    ok = parse_idx1_frames(avi, idx1_off, idx1_sz, movi_list_type_off,
                           movi_data_off);
  }
  if (ok != 0) {
    ok = scan_movi_for_frames(avi, movi_data_off, movi_data_size);
  }
  if (ok != 0 || avi->frame_count == 0) {
    printk(KERN_ERR "AVI: Failed to index MJPEG frames\n");
    avi_mjpeg_close(avi);
    return -1;
  }

  if (avi->width == 0 || avi->height == 0) {
    /* Some files omit/lie; leave as 0 and let renderer handle. */
    printk(KERN_WARNING "AVI: Missing width/height in headers\n");
  }

  return 0;
}

void avi_mjpeg_close(avi_mjpeg_t *avi) {
  if (!avi)
    return;
  if (avi->frame_offsets) {
    kfree(avi->frame_offsets);
    avi->frame_offsets = NULL;
  }
  if (avi->frame_sizes) {
    kfree(avi->frame_sizes);
    avi->frame_sizes = NULL;
  }
  avi->frame_count = 0;
  avi->data = NULL;
  avi->size = 0;
  avi->width = 0;
  avi->height = 0;
  avi->total_frames = 0;
  avi->us_per_frame = 0;
}

int avi_mjpeg_get_frame(const avi_mjpeg_t *avi, uint32_t index,
                        const uint8_t **out_data, size_t *out_size) {
  if (!avi || !out_data || !out_size)
    return -1;
  if (!avi->data || !avi->frame_offsets || !avi->frame_sizes)
    return -1;
  if (index >= avi->frame_count)
    return -1;

  uint32_t off = avi->frame_offsets[index];
  uint32_t sz = avi->frame_sizes[index];
  if ((size_t)off + (size_t)sz > avi->size)
    return -1;

  *out_data = avi->data + off;
  *out_size = (size_t)sz;
  return 0;
}

