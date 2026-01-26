/*
 * Vib-OS - Virtio MMIO 9P (Plan 9) host share driver (minimal)
 *
 * Implements just enough of 9P2000 to read files from a QEMU shared folder.
 * Intended for quick dev import (Option A) rather than a full filesystem.
 */

#include "drivers/virtio_9p.h"
#include "arch/arch.h"
#include "mm/kmalloc.h"
#include "printk.h"
#include "string.h"
#include "types.h"

/* Virtio MMIO layout (matches virtio_net.c pattern) */
#define VIRTIO_MMIO_BASE 0x0a000000
#define VIRTIO_MMIO_STRIDE 0x200

#define VIRTIO_MMIO_MAGIC 0x000
#define VIRTIO_MMIO_VERSION 0x004
#define VIRTIO_MMIO_DEVICE_ID 0x008
#define VIRTIO_MMIO_VENDOR_ID 0x00c
#define VIRTIO_MMIO_DEVICE_FEATURES 0x010
#define VIRTIO_MMIO_DRIVER_FEATURES 0x020
#define VIRTIO_MMIO_QUEUE_SEL 0x030
#define VIRTIO_MMIO_QUEUE_NUM_MAX 0x034
#define VIRTIO_MMIO_QUEUE_NUM 0x038
#define VIRTIO_MMIO_QUEUE_READY 0x044
#define VIRTIO_MMIO_QUEUE_NOTIFY 0x050
#define VIRTIO_MMIO_INTERRUPT_STATUS 0x060
#define VIRTIO_MMIO_INTERRUPT_ACK 0x064
#define VIRTIO_MMIO_STATUS 0x070
#define VIRTIO_MMIO_QUEUE_DESC_LOW 0x080
#define VIRTIO_MMIO_QUEUE_DESC_HIGH 0x084
#define VIRTIO_MMIO_QUEUE_AVAIL_LOW 0x090
#define VIRTIO_MMIO_QUEUE_AVAIL_HIGH 0x094
#define VIRTIO_MMIO_QUEUE_USED_LOW 0x0a0
#define VIRTIO_MMIO_QUEUE_USED_HIGH 0x0a4

#define VIRTIO_STATUS_ACK 1
#define VIRTIO_STATUS_DRIVER 2
#define VIRTIO_STATUS_DRIVER_OK 4
#define VIRTIO_STATUS_FEATURES_OK 8

#define VIRTIO_DEV_9P 9

/* Virtqueue structures */
struct virtq_desc {
  uint64_t addr;
  uint32_t len;
  uint16_t flags;
  uint16_t next;
} __attribute__((packed));

struct virtq_avail {
  uint16_t flags;
  uint16_t idx;
  uint16_t ring[32];
} __attribute__((packed));

struct virtq_used_elem {
  uint32_t id;
  uint32_t len;
} __attribute__((packed));

struct virtq_used {
  uint16_t flags;
  uint16_t idx;
  struct virtq_used_elem ring[32];
} __attribute__((packed));

#define VIRTQ_DESC_F_NEXT 1
#define VIRTQ_DESC_F_WRITE 2

/* 9P message types (9P2000) */
#define P9_TVERSION 100
#define P9_RVERSION 101
#define P9_TATTACH 104
#define P9_RATTACH 105
#define P9_TWALK 110
#define P9_RWALK 111
#define P9_TOPEN 112
#define P9_ROPEN 113
#define P9_TREAD 116
#define P9_RREAD 117
#define P9_TCLUNK 120
#define P9_RCLUNK 121
#define P9_RERROR 107

/* 9P2000.L extension: directory listing */
#define P9_TREADDIR 40
#define P9_RREADDIR 41

#define P9_NOTAG 0xFFFF
#define P9_NOFID 0xFFFFFFFFu

static volatile uint32_t *v9p_base = NULL;
static int v9p_ready = 0;
static char v9p_tag[64] = {0};
static char v9p_last_error[128] = {0};
static uint8_t v9p_last_reply_type = 0;
static uint16_t v9p_last_reply_tag = 0;
static uint32_t v9p_last_reply_len = 0;

static struct {
  struct virtq_desc *desc;
  struct virtq_avail *avail;
  struct virtq_used *used;
  uint16_t last_used;

  uint8_t *tx;
  uint8_t *rx;
  uint32_t buf_size;
  void *qmem;
  uint32_t qsz;
} v9p_q = {0};

static uint16_t next_tag = 1;
static uint32_t root_fid = 1;
static uint32_t tmp_fid = 2;
static uint32_t connected = 0;

const char *virtio_9p_get_tag(void) { return v9p_tag[0] ? v9p_tag : ""; }
const char *virtio_9p_get_last_error(void) {
  return v9p_last_error[0] ? v9p_last_error : "";
}

static void v9p_set_error(const char *msg) {
  int i = 0;
  while (msg && msg[i] && i < (int)sizeof(v9p_last_error) - 1) {
    v9p_last_error[i] = msg[i];
    i++;
  }
  v9p_last_error[i] = '\0';
}

static void mmio_barrier(void) {
#ifdef ARCH_ARM64
  __asm__ volatile("dsb sy" ::: "memory");
#elif defined(ARCH_X86_64) || defined(ARCH_X86)
  __asm__ volatile("mfence" ::: "memory");
#else
  __asm__ volatile("" ::: "memory");
#endif
}

static inline uint32_t mmio_read32(volatile uint32_t *base, uint32_t off) {
  uint32_t v = base[off / 4];
  mmio_barrier();
  return v;
}
static inline void mmio_write32(volatile uint32_t *base, uint32_t off,
                                uint32_t v) {
  mmio_barrier();
  base[off / 4] = v;
  mmio_barrier();
}

static volatile uint32_t *find_virtio_9p(void) {
  for (int i = 0; i < 32; i++) {
    volatile uint32_t *base =
        (volatile uint32_t *)(uintptr_t)(VIRTIO_MMIO_BASE + i * VIRTIO_MMIO_STRIDE);
    uint32_t magic = mmio_read32(base, VIRTIO_MMIO_MAGIC);
    uint32_t dev = mmio_read32(base, VIRTIO_MMIO_DEVICE_ID);
    if (magic == 0x74726976 && dev == VIRTIO_DEV_9P) {
      return base;
    }
  }
  return NULL;
}

static inline void wr_u16le(uint8_t *b, size_t *o, uint16_t v) {
  b[(*o)++] = (uint8_t)(v & 0xFF);
  b[(*o)++] = (uint8_t)((v >> 8) & 0xFF);
}
static inline void wr_u32le(uint8_t *b, size_t *o, uint32_t v) {
  b[(*o)++] = (uint8_t)(v & 0xFF);
  b[(*o)++] = (uint8_t)((v >> 8) & 0xFF);
  b[(*o)++] = (uint8_t)((v >> 16) & 0xFF);
  b[(*o)++] = (uint8_t)((v >> 24) & 0xFF);
}
static inline void wr_u64le(uint8_t *b, size_t *o, uint64_t v) {
  wr_u32le(b, o, (uint32_t)(v & 0xFFFFFFFFu));
  wr_u32le(b, o, (uint32_t)(v >> 32));
}
static inline void patch_u32le(uint8_t *b, uint32_t v) {
  b[0] = (uint8_t)(v & 0xFF);
  b[1] = (uint8_t)((v >> 8) & 0xFF);
  b[2] = (uint8_t)((v >> 16) & 0xFF);
  b[3] = (uint8_t)((v >> 24) & 0xFF);
}
static inline uint16_t rd_u16le(const uint8_t *b, size_t *o) {
  uint16_t v = (uint16_t)b[*o] | ((uint16_t)b[*o + 1] << 8);
  *o += 2;
  return v;
}
static inline uint32_t rd_u32le(const uint8_t *b, size_t *o) {
  uint32_t v = (uint32_t)b[*o] | ((uint32_t)b[*o + 1] << 8) |
               ((uint32_t)b[*o + 2] << 16) | ((uint32_t)b[*o + 3] << 24);
  *o += 4;
  return v;
}
static inline uint64_t rd_u64le(const uint8_t *b, size_t *o) {
  uint64_t lo = (uint64_t)rd_u32le(b, o);
  uint64_t hi = (uint64_t)rd_u32le(b, o);
  return lo | (hi << 32);
}
static inline void wr_str(uint8_t *b, size_t *o, const char *s) {
  uint16_t len = 0;
  while (s && s[len])
    len++;
  wr_u16le(b, o, len);
  for (uint16_t i = 0; i < len; i++)
    b[(*o)++] = (uint8_t)s[i];
}

static int v9p_submit(const uint8_t *req, uint32_t req_len, uint16_t *out_desc) {
  if (!v9p_ready)
    return -1;
  if (req_len > v9p_q.buf_size)
    return -1;

  /* Single outstanding request, use desc 0 and 1 */
  for (uint32_t i = 0; i < req_len; i++)
    v9p_q.tx[i] = req[i];

  v9p_q.desc[0].addr = (uint64_t)(uintptr_t)v9p_q.tx;
  v9p_q.desc[0].len = req_len;
  v9p_q.desc[0].flags = VIRTQ_DESC_F_NEXT;
  v9p_q.desc[0].next = 1;

  v9p_q.desc[1].addr = (uint64_t)(uintptr_t)v9p_q.rx;
  v9p_q.desc[1].len = v9p_q.buf_size;
  v9p_q.desc[1].flags = VIRTQ_DESC_F_WRITE;
  v9p_q.desc[1].next = 0;

  /* Ensure tx buffer + descriptor writes are visible before enqueue. */
  mmio_barrier();

  uint16_t idx = v9p_q.avail->idx;
  uint16_t qsz = (uint16_t)(v9p_q.qsz ? v9p_q.qsz : 32);
  v9p_q.avail->ring[idx % qsz] = 0;
  mmio_barrier();
  v9p_q.avail->idx = idx + 1;
  mmio_barrier();

  mmio_write32(v9p_base, VIRTIO_MMIO_QUEUE_NOTIFY, 0);

  if (out_desc)
    *out_desc = 0;
  return 0;
}

static int v9p_wait_resp(uint32_t *out_len) {
  if (!v9p_ready)
    return -1;
  /* Poll used ring (time-based so it works across hosts/accelerators). */
  uint64_t start_ms = arch_timer_get_ms();
  while ((arch_timer_get_ms() - start_ms) < 2000u) {
    uint16_t used_idx = v9p_q.used->idx;
    if (v9p_q.last_used != used_idx) {
      uint16_t qsz = (uint16_t)(v9p_q.qsz ? v9p_q.qsz : 32);
      uint16_t ring_idx = v9p_q.last_used % qsz;
      mmio_barrier();
      uint32_t len = v9p_q.used->ring[ring_idx].len;
      v9p_q.last_used++;
      if (out_len)
        *out_len = len;
      return 0;
    }
  }
  v9p_set_error("9p timeout");
  return -1;
}

static int v9p_rpc(uint8_t *msg, uint32_t msg_len, uint8_t expect_type,
                   uint16_t expect_tag, uint32_t *out_rx_len) {
  uint16_t desc = 0;
  if (v9p_submit(msg, msg_len, &desc) != 0) {
    v9p_set_error("9p submit failed");
    return -1;
  }
  (void)desc;

  uint32_t rx_len = 0;
  if (v9p_wait_resp(&rx_len) != 0)
    return -1;
  v9p_last_reply_len = rx_len;

  if (rx_len < 7) {
    v9p_set_error("9p short reply");
    return -1;
  }
  size_t o = 0;
  uint32_t sz = rd_u32le(v9p_q.rx, &o);
  uint8_t type = v9p_q.rx[o++];
  uint16_t tag = rd_u16le(v9p_q.rx, &o);
  v9p_last_reply_type = type;
  v9p_last_reply_tag = tag;
  if (sz > rx_len)
  {
    v9p_set_error("9p truncated reply");
    return -1;
  }

  if (type == P9_RERROR) {
    /* Rerror: ename(string) [and errno for 9P2000.L; ignore extra] */
    uint16_t elen = rd_u16le(v9p_q.rx, &o);
    char tmp[128];
    int ti = 0;
    for (uint16_t i = 0; i < elen && o < sz && ti < (int)sizeof(tmp) - 1; i++) {
      tmp[ti++] = (char)v9p_q.rx[o++];
    }
    tmp[ti] = '\0';
    v9p_set_error(tmp[0] ? tmp : "Rerror");
    return -1;
  }

  if (type != expect_type || tag != expect_tag) {
    v9p_set_error("unexpected 9p reply");
    return -1;
  }
  if (out_rx_len)
    *out_rx_len = sz;
  return 0;
}

static char v9p_server_version[16] = {0};

static int v9p_is_l(void) {
  for (int i = 0; v9p_server_version[i]; i++) {
    if (v9p_server_version[i] == '.' && v9p_server_version[i + 1] == 'L') {
      return 1;
    }
  }
  return 0;
}

static int v9p_connect(void) {
  if (connected)
    return 0;
  if (!v9p_ready)
    return -1;

  static int logged_rversion_fail = 0;
  static int logged_rattach_fail = 0;

  /* TVERSION */
  uint8_t msg[256];
  size_t o = 0;
  wr_u32le(msg, &o, 0);            /* size patch */
  msg[o++] = P9_TVERSION;
  wr_u16le(msg, &o, P9_NOTAG);
  wr_u32le(msg, &o, v9p_q.buf_size);
  wr_str(msg, &o, "9P2000.L");
  patch_u32le(msg, (uint32_t)o);
  uint32_t rx_sz = 0;
  if (v9p_rpc(msg, (uint32_t)o, P9_RVERSION, P9_NOTAG, &rx_sz) != 0) {
    if (!logged_rversion_fail) {
      const char *err = virtio_9p_get_last_error();
      if (err && err[0]) {
        printk(KERN_ERR "9P: Rversion failed (%s, type=%u tag=0x%x len=%u)\n",
               err, (unsigned)v9p_last_reply_type,
               (unsigned)v9p_last_reply_tag, (unsigned)v9p_last_reply_len);
      } else {
        printk(KERN_ERR "9P: Rversion failed\n");
      }
      logged_rversion_fail = 1;
    }
    return -1;
  }
  logged_rversion_fail = 0;
  /* Parse server version string to decide attach format. */
  uint32_t server_msize = 0;
  {
    size_t ro = 7;
    server_msize = rd_u32le(v9p_q.rx, &ro); /* msize */
    uint16_t vlen = rd_u16le(v9p_q.rx, &ro);
    int out = 0;
    for (uint16_t i = 0; i < vlen && ro < rx_sz &&
                         out < (int)sizeof(v9p_server_version) - 1;
         i++) {
      char c = (char)v9p_q.rx[ro++];
      if ((unsigned char)c < 32)
        c = '?';
      v9p_server_version[out++] = c;
    }
    v9p_server_version[out] = '\0';
  }
  /* Respect server-negotiated msize (keep buffers allocated as-is). */
  if (server_msize >= 256 && server_msize < v9p_q.buf_size) {
    v9p_q.buf_size = server_msize;
  }

  /* TATTACH root fid */
  o = 0;
  wr_u32le(msg, &o, 0);
  msg[o++] = P9_TATTACH;
  uint16_t tag = next_tag++;
  wr_u16le(msg, &o, tag);
  wr_u32le(msg, &o, root_fid);
  wr_u32le(msg, &o, P9_NOFID);
  wr_str(msg, &o, "root"); /* uname */
  wr_str(msg, &o, "");     /* aname */
  /* 9P2000.L adds n_uname (numeric uid) */
  if (v9p_is_l()) {
    wr_u32le(msg, &o, 0); /* n_uname */
  }
  patch_u32le(msg, (uint32_t)o);
  if (v9p_rpc(msg, (uint32_t)o, P9_RATTACH, tag, &rx_sz) != 0) {
    if (!logged_rattach_fail) {
      const char *err = virtio_9p_get_last_error();
      if (err && err[0]) {
        printk(KERN_ERR "9P: Rattach failed (%s, type=%u tag=0x%x len=%u)\n",
               err, (unsigned)v9p_last_reply_type,
               (unsigned)v9p_last_reply_tag, (unsigned)v9p_last_reply_len);
      } else {
        printk(KERN_ERR "9P: Rattach failed\n");
      }
      logged_rattach_fail = 1;
    }
    return -1;
  }
  logged_rattach_fail = 0;

  connected = 1;
  printk(KERN_INFO "9P: connected (proto='%s', msize=%u)\n",
         v9p_server_version[0] ? v9p_server_version : "unknown",
         (unsigned)v9p_q.buf_size);
  return 0;
}

static int v9p_clunk(uint32_t fid) {
  uint8_t msg[64];
  size_t o = 0;
  wr_u32le(msg, &o, 0);
  msg[o++] = P9_TCLUNK;
  uint16_t tag = next_tag++;
  wr_u16le(msg, &o, tag);
  wr_u32le(msg, &o, fid);
  patch_u32le(msg, (uint32_t)o);
  uint32_t rx_sz = 0;
  return v9p_rpc(msg, (uint32_t)o, P9_RCLUNK, tag, &rx_sz);
}

static int v9p_walk(uint32_t newfid, const char *path) {
  uint8_t msg[512];

  /* Split path into elements */
  const char *p = path;
  while (*p == '/')
    p++;

  const char *parts[32];
  uint16_t part_count = 0;
  while (*p && part_count < 32) {
    parts[part_count++] = p;
    while (*p && *p != '/')
      p++;
    while (*p == '/')
      p++;
  }

  size_t o = 0;
  wr_u32le(msg, &o, 0);
  msg[o++] = P9_TWALK;
  uint16_t tag = next_tag++;
  wr_u16le(msg, &o, tag);
  wr_u32le(msg, &o, root_fid);
  wr_u32le(msg, &o, newfid);
  wr_u16le(msg, &o, part_count);

  for (uint16_t i = 0; i < part_count; i++) {
    const char *s = parts[i];
    uint16_t len = 0;
    while (s[len] && s[len] != '/')
      len++;
    wr_u16le(msg, &o, len);
    for (uint16_t j = 0; j < len; j++)
      msg[o++] = (uint8_t)s[j];
  }

  patch_u32le(msg, (uint32_t)o);
  uint32_t rx_sz = 0;
  if (v9p_rpc(msg, (uint32_t)o, P9_RWALK, tag, &rx_sz) != 0)
    return -1;

  /* Validate nwqid */
  size_t ro = 7;
  uint16_t nwqid = rd_u16le(v9p_q.rx, &ro);
  if (nwqid != part_count) {
    return -1;
  }
  return 0;
}

static int v9p_open(uint32_t fid) {
  uint8_t msg[64];
  size_t o = 0;
  wr_u32le(msg, &o, 0);
  msg[o++] = P9_TOPEN;
  uint16_t tag = next_tag++;
  wr_u16le(msg, &o, tag);
  wr_u32le(msg, &o, fid);
  if (v9p_is_l()) {
    /* 9P2000.L uses full open flags (u32) */
    wr_u32le(msg, &o, 0); /* O_RDONLY */
  } else {
    msg[o++] = 0; /* OREAD */
  }
  patch_u32le(msg, (uint32_t)o);
  uint32_t rx_sz = 0;
  return v9p_rpc(msg, (uint32_t)o, P9_ROPEN, tag, &rx_sz);
}

static int v9p_read(uint32_t fid, uint64_t offset, uint32_t count,
                    uint8_t **out_ptr, uint32_t *out_count) {
  uint8_t msg[64];
  size_t o = 0;
  wr_u32le(msg, &o, 0);
  msg[o++] = P9_TREAD;
  uint16_t tag = next_tag++;
  wr_u16le(msg, &o, tag);
  wr_u32le(msg, &o, fid);
  wr_u64le(msg, &o, offset);
  wr_u32le(msg, &o, count);
  patch_u32le(msg, (uint32_t)o);

  uint32_t rx_sz = 0;
  if (v9p_rpc(msg, (uint32_t)o, P9_RREAD, tag, &rx_sz) != 0)
    return -1;
  /* Rread payload: count + data */
  size_t ro = 7;
  uint32_t got = rd_u32le(v9p_q.rx, &ro);
  if (7 + 4 + got > rx_sz)
    return -1;
  if (out_ptr)
    *out_ptr = v9p_q.rx + ro;
  if (out_count)
    *out_count = got;
  return 0;
}

static int v9p_readdir_rpc(uint32_t fid, uint64_t offset, uint32_t count,
                           uint8_t **out_ptr, uint32_t *out_count) {
  uint8_t msg[64];
  size_t o = 0;
  wr_u32le(msg, &o, 0);
  msg[o++] = P9_TREADDIR;
  uint16_t tag = next_tag++;
  wr_u16le(msg, &o, tag);
  wr_u32le(msg, &o, fid);
  wr_u64le(msg, &o, offset);
  wr_u32le(msg, &o, count);
  patch_u32le(msg, (uint32_t)o);

  uint32_t rx_sz = 0;
  if (v9p_rpc(msg, (uint32_t)o, P9_RREADDIR, tag, &rx_sz) != 0)
    return -1;

  size_t ro = 7;
  uint32_t got = rd_u32le(v9p_q.rx, &ro);
  if (7 + 4 + got > rx_sz)
    return -1;
  if (out_ptr)
    *out_ptr = v9p_q.rx + ro;
  if (out_count)
    *out_count = got;
  return 0;
}

int virtio_9p_init(void) {
  if (v9p_ready)
    return 0;

  v9p_base = find_virtio_9p();
  if (!v9p_base) {
    v9p_set_error("no virtio-9p device");
    return -1;
  }

  mmio_write32(v9p_base, VIRTIO_MMIO_STATUS, 0);
  mmio_write32(v9p_base, VIRTIO_MMIO_STATUS, VIRTIO_STATUS_ACK | VIRTIO_STATUS_DRIVER);

  /* No features negotiated */
  mmio_write32(v9p_base, VIRTIO_MMIO_DRIVER_FEATURES, 0);
  mmio_write32(v9p_base, VIRTIO_MMIO_STATUS,
               VIRTIO_STATUS_ACK | VIRTIO_STATUS_DRIVER |
                   VIRTIO_STATUS_FEATURES_OK);

  uint32_t st = mmio_read32(v9p_base, VIRTIO_MMIO_STATUS);
  if (!(st & VIRTIO_STATUS_FEATURES_OK)) {
    printk(KERN_WARNING "9P: FEATURES_OK not accepted\n");
    return -1;
  }

  /* Queue 0 */
  mmio_write32(v9p_base, VIRTIO_MMIO_QUEUE_SEL, 0);
  uint32_t max = mmio_read32(v9p_base, VIRTIO_MMIO_QUEUE_NUM_MAX);
  if (max == 0) {
    printk(KERN_ERR "9P: no queue\n");
    return -1;
  }

  /* Use a fixed size to match our ring arrays. */
  uint32_t qsz = (max >= 32) ? 32 : max;
  if (qsz < 2) {
    printk(KERN_ERR "9P: queue too small (%u)\n", qsz);
    return -1;
  }
  v9p_q.qsz = qsz;
  mmio_write32(v9p_base, VIRTIO_MMIO_QUEUE_NUM, qsz);

  /* Allocate a single, aligned block for descriptors/avail/used (DMA). */
  size_t need = 4096 * 2;
  v9p_q.qmem = kmalloc(need, GFP_KERNEL);
  if (!v9p_q.qmem) {
    printk(KERN_ERR "9P: queue mem alloc failed\n");
    return -1;
  }
  uintptr_t base = (uintptr_t)v9p_q.qmem;
  base = (base + 4095) & ~4095;

  v9p_q.desc = (struct virtq_desc *)base;
  v9p_q.avail =
      (struct virtq_avail *)(base + qsz * sizeof(struct virtq_desc));
  v9p_q.used = (struct virtq_used *)(base + 2048); /* Simplified offset */

  memset(v9p_q.desc, 0, sizeof(struct virtq_desc) * qsz);
  memset(v9p_q.avail, 0, sizeof(struct virtq_avail));
  memset(v9p_q.used, 0, sizeof(struct virtq_used));

  v9p_q.buf_size = 32768;
  v9p_q.tx = (uint8_t *)kmalloc(v9p_q.buf_size, GFP_KERNEL);
  v9p_q.rx = (uint8_t *)kmalloc(v9p_q.buf_size, GFP_KERNEL);
  if (!v9p_q.tx || !v9p_q.rx) {
    printk(KERN_ERR "9P: buffers alloc failed\n");
    return -1;
  }

  mmio_write32(v9p_base, VIRTIO_MMIO_QUEUE_DESC_LOW, (uint32_t)(uintptr_t)v9p_q.desc);
  mmio_write32(v9p_base, VIRTIO_MMIO_QUEUE_DESC_HIGH,
               (uint32_t)((uint64_t)(uintptr_t)v9p_q.desc >> 32));
  mmio_write32(v9p_base, VIRTIO_MMIO_QUEUE_AVAIL_LOW, (uint32_t)(uintptr_t)v9p_q.avail);
  mmio_write32(v9p_base, VIRTIO_MMIO_QUEUE_AVAIL_HIGH,
               (uint32_t)((uint64_t)(uintptr_t)v9p_q.avail >> 32));
  mmio_write32(v9p_base, VIRTIO_MMIO_QUEUE_USED_LOW, (uint32_t)(uintptr_t)v9p_q.used);
  mmio_write32(v9p_base, VIRTIO_MMIO_QUEUE_USED_HIGH,
               (uint32_t)((uint64_t)(uintptr_t)v9p_q.used >> 32));
  mmio_write32(v9p_base, VIRTIO_MMIO_QUEUE_READY, 1);

  mmio_write32(v9p_base, VIRTIO_MMIO_STATUS,
               VIRTIO_STATUS_ACK | VIRTIO_STATUS_DRIVER |
                   VIRTIO_STATUS_FEATURES_OK | VIRTIO_STATUS_DRIVER_OK);

  v9p_q.last_used = 0;
  v9p_ready = 1;
  connected = 0;
  v9p_last_error[0] = '\0';

  /* Read mount tag from device config (best-effort). */
  {
    volatile uint8_t *cfg = (volatile uint8_t *)((uintptr_t)v9p_base + 0x100);
    uint16_t taglen = (uint16_t)cfg[0] | ((uint16_t)cfg[1] << 8);
    int out = 0;
    if (taglen > 0) {
      for (uint16_t i = 0; i < taglen && out < (int)sizeof(v9p_tag) - 1; i++) {
        char c = (char)cfg[2 + i];
        if ((unsigned char)c < 32)
          c = '?';
        v9p_tag[out++] = c;
      }
    }
    v9p_tag[out] = '\0';
  }

  printk(KERN_INFO "9P: virtio-9p ready (tag='%s')\n", virtio_9p_get_tag());
  return 0;
}

int virtio_9p_is_ready(void) { return v9p_ready; }

int virtio_9p_read_file(const char *path, uint8_t **out_data, size_t *out_size,
                        size_t max_bytes) {
  if (!path || !out_data || !out_size)
    return -1;

  if (!v9p_ready) {
    if (virtio_9p_init() != 0)
      return -1;
  }

  if (v9p_connect() != 0)
    return -1;

  uint32_t fid = tmp_fid++;
  if (fid == root_fid)
    fid = tmp_fid++;

  if (v9p_walk(fid, path) != 0) {
    v9p_clunk(fid);
    return -1;
  }
  if (v9p_open(fid) != 0) {
    v9p_clunk(fid);
    return -1;
  }

  size_t cap = 0;
  size_t len = 0;
  uint8_t *buf = NULL;
  uint64_t off = 0;

  for (;;) {
    uint8_t *ptr = NULL;
    uint32_t got = 0;
    uint32_t want = v9p_q.buf_size - 64;
    if (want > 16384)
      want = 16384;
    if (v9p_read(fid, off, want, &ptr, &got) != 0) {
      kfree(buf);
      v9p_clunk(fid);
      return -1;
    }
    if (got == 0)
      break;
    if (len + got > max_bytes) {
      kfree(buf);
      v9p_clunk(fid);
      return -1;
    }
    if (len + got > cap) {
      size_t new_cap = cap ? cap * 2 : 4096;
      while (new_cap < len + got)
        new_cap *= 2;
      if (new_cap > max_bytes)
        new_cap = max_bytes;
      uint8_t *nb = (uint8_t *)kmalloc(new_cap, GFP_KERNEL);
      if (!nb) {
        kfree(buf);
        v9p_clunk(fid);
        return -1;
      }
      for (size_t i = 0; i < len; i++)
        nb[i] = buf[i];
      if (buf)
        kfree(buf);
      buf = nb;
      cap = new_cap;
    }
    for (uint32_t i = 0; i < got; i++)
      buf[len + i] = ptr[i];
    len += got;
    off += got;
  }

  v9p_clunk(fid);
  *out_data = buf;
  *out_size = len;
  return 0;
}

int virtio_9p_readdir(const char *path, void *ctx,
                      int (*filldir)(void *, const char *, int, loff_t, ino_t,
                                     unsigned)) {
  if (!filldir)
    return -1;

  const char *p = path ? path : "";

  if (!v9p_ready) {
    if (virtio_9p_init() != 0)
      return -1;
  }
  if (v9p_connect() != 0)
    return -1;

  /* 9P2000.L uses Treaddir/Rreaddir instead of Tread on directories. */
  if (v9p_is_l()) {
    uint32_t fid = tmp_fid++;
    if (fid == root_fid)
      fid = tmp_fid++;

    if (v9p_walk(fid, p) != 0) {
      v9p_clunk(fid);
      return -1;
    }
    if (v9p_open(fid) != 0) {
      v9p_clunk(fid);
      return -1;
    }

    uint64_t off = 0;
    for (int iter = 0; iter < 4096; iter++) {
      uint8_t *ptr = NULL;
      uint32_t got = 0;
      uint32_t want = v9p_q.buf_size - 64;
      if (want > 16384)
        want = 16384;

      uint64_t last_off = off;
      if (v9p_readdir_rpc(fid, off, want, &ptr, &got) != 0) {
        v9p_clunk(fid);
        return -1;
      }
      if (got == 0)
        break;

      size_t ro = 0;
      int stop = 0;
      while (ro < got) {
        /* dirent: qid(13) + offset(8) + type(1) + name(string) */
        if (ro + 13 + 8 + 1 + 2 > got)
          break;
        ro += 13; /* qid */
        off = rd_u64le(ptr, &ro);
        uint8_t dtype = ptr[ro++];
        uint16_t name_len = rd_u16le(ptr, &ro);
        if (ro + name_len > got)
          break;

        char name[256];
        int ncopy = (name_len < (uint16_t)(sizeof(name) - 1))
                        ? (int)name_len
                        : (int)(sizeof(name) - 1);
        for (int i = 0; i < ncopy; i++)
          name[i] = (char)ptr[ro + (size_t)i];
        name[ncopy] = '\0';
        ro += name_len;

        int rc = filldir(ctx, name, ncopy, 0, 0, (unsigned)dtype);
        if (rc) {
          stop = 1;
          break;
        }
      }

      if (stop)
        break;
      if (off == last_off)
        break;
    }

    v9p_clunk(fid);
    return 0;
  }

  /* 9P2000 / 9P2000.u: reading a directory returns packed stat structures. */
  uint8_t *data = NULL;
  size_t sz = 0;
  if (virtio_9p_read_file(p, &data, &sz, 512u * 1024u) != 0) {
    return -1;
  }

  size_t off = 0;
  while (off + 2 <= sz) {
    size_t o = off;
    uint16_t st_size = rd_u16le(data, &o);
    if (o + st_size > sz) {
      break;
    }

    size_t end = o + st_size;
    size_t s = o;

    /* Minimum stat payload length for fields up to name length. */
    if (s + 2 + 4 + 13 + 4 + 4 + 4 + 8 + 2 > end) {
      off = end;
      continue;
    }

    (void)rd_u16le(data, &s); /* type */
    (void)rd_u32le(data, &s); /* dev */
    if (s + 13 > end) {
      off = end;
      continue;
    }
    s += 13; /* qid */

    uint32_t mode = rd_u32le(data, &s);
    s += 4; /* atime */
    s += 4; /* mtime */
    s += 8; /* length */

    uint16_t name_len = rd_u16le(data, &s);
    if (s + name_len > end) {
      off = end;
      continue;
    }

    char name[256];
    int ncopy = (name_len < (uint16_t)(sizeof(name) - 1))
                    ? (int)name_len
                    : (int)(sizeof(name) - 1);
    for (int i = 0; i < ncopy; i++)
      name[i] = (char)data[s + (size_t)i];
    name[ncopy] = '\0';

    /* Plan 9 directory bit (DMDIR). */
    unsigned type = (mode & 0x80000000u) ? 4u : 8u;

    int rc = filldir(ctx, name, ncopy, 0, 0, type);
    if (rc) {
      break;
    }

    off = end;
  }

  kfree(data);
  return 0;
}
