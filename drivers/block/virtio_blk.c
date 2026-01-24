/*
 * Vib-OS - VirtIO Block Device Driver
 *
 * Provides persistent storage through QEMU's virtio-blk device.
 * Supports read/write operations for FAT32/ext4 filesystems.
 *
 * Based on VirtIO 1.0 specification and QEMU virt machine layout.
 */

#include "types.h"
#include "printk.h"
#include "mm/kmalloc.h"

/* ===================================================================== */
/* VirtIO MMIO registers (QEMU virt machine) */
/* ===================================================================== */

#define VIRTIO_MMIO_BASE        0x0a000000
#define VIRTIO_MMIO_STRIDE      0x200

/* VirtIO MMIO register offsets */
#define VIRTIO_MMIO_MAGIC           0x000
#define VIRTIO_MMIO_VERSION         0x004
#define VIRTIO_MMIO_DEVICE_ID       0x008
#define VIRTIO_MMIO_VENDOR_ID       0x00c
#define VIRTIO_MMIO_DEVICE_FEATURES 0x010
#define VIRTIO_MMIO_DEVICE_FEATURES_SEL 0x014
#define VIRTIO_MMIO_DRIVER_FEATURES 0x020
#define VIRTIO_MMIO_DRIVER_FEATURES_SEL 0x024
#define VIRTIO_MMIO_QUEUE_SEL       0x030
#define VIRTIO_MMIO_QUEUE_NUM_MAX   0x034
#define VIRTIO_MMIO_QUEUE_NUM       0x038
#define VIRTIO_MMIO_QUEUE_READY     0x044
#define VIRTIO_MMIO_QUEUE_NOTIFY    0x050
#define VIRTIO_MMIO_INTERRUPT_STATUS 0x060
#define VIRTIO_MMIO_INTERRUPT_ACK   0x064
#define VIRTIO_MMIO_STATUS          0x070
#define VIRTIO_MMIO_QUEUE_DESC_LOW  0x080
#define VIRTIO_MMIO_QUEUE_DESC_HIGH 0x084
#define VIRTIO_MMIO_QUEUE_AVAIL_LOW 0x090
#define VIRTIO_MMIO_QUEUE_AVAIL_HIGH 0x094
#define VIRTIO_MMIO_QUEUE_USED_LOW  0x0a0
#define VIRTIO_MMIO_QUEUE_USED_HIGH 0x0a4
#define VIRTIO_MMIO_CONFIG          0x100

/* VirtIO device status bits */
#define VIRTIO_STATUS_ACK           1
#define VIRTIO_STATUS_DRIVER        2
#define VIRTIO_STATUS_DRIVER_OK     4
#define VIRTIO_STATUS_FEATURES_OK   8
#define VIRTIO_STATUS_FAILED        128

/* VirtIO device types */
#define VIRTIO_DEV_BLK              2

/* VirtIO block features */
#define VIRTIO_BLK_F_SIZE_MAX       (1 << 1)
#define VIRTIO_BLK_F_SEG_MAX        (1 << 2)
#define VIRTIO_BLK_F_GEOMETRY       (1 << 4)
#define VIRTIO_BLK_F_RO             (1 << 5)
#define VIRTIO_BLK_F_BLK_SIZE       (1 << 6)
#define VIRTIO_BLK_F_FLUSH          (1 << 9)

/* VirtIO block request types */
#define VIRTIO_BLK_T_IN             0   /* Read */
#define VIRTIO_BLK_T_OUT            1   /* Write */
#define VIRTIO_BLK_T_FLUSH          4   /* Flush */

/* VirtIO block status codes */
#define VIRTIO_BLK_S_OK             0
#define VIRTIO_BLK_S_IOERR          1
#define VIRTIO_BLK_S_UNSUPP         2

/* Sector size */
#define SECTOR_SIZE                 512

/* ===================================================================== */
/* VirtIO structures */
/* ===================================================================== */

/* VirtIO descriptor */
typedef struct __attribute__((packed, aligned(16))) {
    uint64_t addr;      /* Physical address of buffer */
    uint32_t len;       /* Length of buffer */
    uint16_t flags;     /* Descriptor flags */
    uint16_t next;      /* Next descriptor index */
} virtq_desc_t;

#define VIRTQ_DESC_F_NEXT       1   /* Buffer continues in next descriptor */
#define VIRTQ_DESC_F_WRITE      2   /* Buffer is write-only (device writes) */

/* VirtIO available ring */
typedef struct __attribute__((packed, aligned(2))) {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[8];
    uint16_t used_event;  /* Only if VIRTIO_F_EVENT_IDX */
} virtq_avail_t;

/* VirtIO used ring element */
typedef struct __attribute__((packed)) {
    uint32_t id;
    uint32_t len;
} virtq_used_elem_t;

/* VirtIO used ring */
typedef struct __attribute__((packed, aligned(4))) {
    uint16_t flags;
    uint16_t idx;
    virtq_used_elem_t ring[8];
    uint16_t avail_event;  /* Only if VIRTIO_F_EVENT_IDX */
} virtq_used_t;

/* VirtIO block request header */
typedef struct __attribute__((packed)) {
    uint32_t type;      /* Request type (IN/OUT/FLUSH) */
    uint32_t reserved;
    uint64_t sector;    /* Sector number */
} virtio_blk_req_t;

/* ===================================================================== */
/* Queue configuration - use small queue for simplicity */
/* ===================================================================== */

#define QUEUE_SIZE          8

/* ===================================================================== */
/* Driver state */
/* ===================================================================== */

static volatile uint32_t *blk_base = NULL;
static bool blk_initialized = false;
static uint64_t blk_capacity = 0;       /* Disk capacity in sectors */
static uint32_t blk_sector_size = 512;  /* Sector size in bytes */

/* Queue structures - properly aligned */
static virtq_desc_t  desc_ring[QUEUE_SIZE] __attribute__((aligned(16)));
static virtq_avail_t avail_ring __attribute__((aligned(4096)));
static virtq_used_t  used_ring __attribute__((aligned(4096)));

static uint16_t last_used_idx = 0;

/* Single request buffer - we process one request at a time for simplicity */
static virtio_blk_req_t request_hdr __attribute__((aligned(16)));
static uint8_t data_buffer[SECTOR_SIZE] __attribute__((aligned(16)));
static uint8_t status_byte __attribute__((aligned(16)));

/* ===================================================================== */
/* MMIO Helpers */
/* ===================================================================== */

static inline void mmio_barrier(void)
{
    asm volatile("dsb sy" ::: "memory");
}

static inline uint32_t mmio_read32(volatile uint32_t *base, uint32_t offset)
{
    volatile uint32_t *addr = (volatile uint32_t *)((uint8_t *)base + offset);
    uint32_t val = *addr;
    mmio_barrier();
    return val;
}

static inline void mmio_write32(volatile uint32_t *base, uint32_t offset, uint32_t val)
{
    volatile uint32_t *addr = (volatile uint32_t *)((uint8_t *)base + offset);
    mmio_barrier();
    *addr = val;
    mmio_barrier();
}

static inline uint64_t mmio_read64(volatile uint32_t *base, uint32_t offset)
{
    uint32_t lo = mmio_read32(base, offset);
    uint32_t hi = mmio_read32(base, offset + 4);
    return ((uint64_t)hi << 32) | lo;
}

/* ===================================================================== */
/* Find VirtIO Block Device */
/* ===================================================================== */

static volatile uint32_t *find_virtio_blk(void)
{
    for (int i = 0; i < 32; i++) {
        volatile uint32_t *base = (volatile uint32_t *)(uintptr_t)(VIRTIO_MMIO_BASE + i * VIRTIO_MMIO_STRIDE);

        uint32_t magic = mmio_read32(base, VIRTIO_MMIO_MAGIC);
        uint32_t device_id = mmio_read32(base, VIRTIO_MMIO_DEVICE_ID);

        if (magic == 0x74726976 && device_id == VIRTIO_DEV_BLK) {
            printk(KERN_INFO "VIRTIO_BLK: Found block device at slot %d\n", i);
            return base;
        }
    }

    return NULL;
}

/* ===================================================================== */
/* Single-Request I/O (synchronous, one at a time) */
/* ===================================================================== */

static int do_block_io(uint32_t type, uint64_t sector, void *buffer)
{
    if (!blk_initialized) {
        return -1;
    }

    /* Setup request header */
    request_hdr.type = type;
    request_hdr.reserved = 0;
    request_hdr.sector = sector;

    /* For writes, copy data to our aligned buffer */
    if (type == VIRTIO_BLK_T_OUT && buffer) {
        uint8_t *src = (uint8_t *)buffer;
        for (int i = 0; i < SECTOR_SIZE; i++) {
            data_buffer[i] = src[i];
        }
    }

    /* Initialize status to invalid value */
    status_byte = 0xFF;

    /* Setup descriptor chain: header -> data -> status */
    /* Descriptor 0: Request header (device reads) */
    desc_ring[0].addr = (uint64_t)(uintptr_t)&request_hdr;
    desc_ring[0].len = sizeof(virtio_blk_req_t);
    desc_ring[0].flags = VIRTQ_DESC_F_NEXT;
    desc_ring[0].next = 1;

    /* Descriptor 1: Data buffer */
    desc_ring[1].addr = (uint64_t)(uintptr_t)data_buffer;
    desc_ring[1].len = SECTOR_SIZE;
    if (type == VIRTIO_BLK_T_IN) {
        /* Read: device writes to buffer */
        desc_ring[1].flags = VIRTQ_DESC_F_WRITE | VIRTQ_DESC_F_NEXT;
    } else {
        /* Write: device reads from buffer */
        desc_ring[1].flags = VIRTQ_DESC_F_NEXT;
    }
    desc_ring[1].next = 2;

    /* Descriptor 2: Status byte (device writes) */
    desc_ring[2].addr = (uint64_t)(uintptr_t)&status_byte;
    desc_ring[2].len = 1;
    desc_ring[2].flags = VIRTQ_DESC_F_WRITE;
    desc_ring[2].next = 0;

    /* Memory barrier before making descriptor available */
    mmio_barrier();

    /* Add descriptor chain head to available ring */
    uint16_t avail_idx = avail_ring.idx;
    avail_ring.ring[avail_idx % QUEUE_SIZE] = 0;  /* First descriptor index */
    mmio_barrier();
    avail_ring.idx = avail_idx + 1;
    mmio_barrier();

    /* Notify device */
    mmio_write32(blk_base, VIRTIO_MMIO_QUEUE_NOTIFY, 0);

    /* Wait for completion - poll used ring */
    int timeout = 10000000;
    while (used_ring.idx == last_used_idx && timeout > 0) {
        mmio_barrier();
        timeout--;
        /* Small delay */
        for (volatile int d = 0; d < 10; d++) { }
    }

    /* Acknowledge interrupt immediately */
    uint32_t isr = mmio_read32(blk_base, VIRTIO_MMIO_INTERRUPT_STATUS);
    if (isr) {
        mmio_write32(blk_base, VIRTIO_MMIO_INTERRUPT_ACK, isr);
    }

    if (timeout == 0) {
        printk(KERN_WARNING "VIRTIO_BLK: Request timeout (sector %llu)\n",
               (unsigned long long)sector);
        return -1;
    }

    /* Update last used index */
    last_used_idx = used_ring.idx;

    /* Check status */
    if (status_byte != VIRTIO_BLK_S_OK) {
        printk(KERN_ERR "VIRTIO_BLK: I/O error at sector %llu, status=%d\n",
               (unsigned long long)sector, status_byte);
        return -1;
    }

    /* For reads, copy data back to caller's buffer */
    if (type == VIRTIO_BLK_T_IN && buffer) {
        uint8_t *dst = (uint8_t *)buffer;
        for (int i = 0; i < SECTOR_SIZE; i++) {
            dst[i] = data_buffer[i];
        }
    }

    return 0;
}

/* ===================================================================== */
/* Block I/O Operations */
/* ===================================================================== */

int virtio_blk_read(uint64_t sector, uint32_t count, void *buffer)
{
    if (!blk_initialized) {
        return -1;
    }

    if (sector + count > blk_capacity) {
        printk(KERN_ERR "VIRTIO_BLK: Read beyond disk capacity\n");
        return -1;
    }

    uint8_t *buf = (uint8_t *)buffer;

    for (uint32_t i = 0; i < count; i++) {
        if (do_block_io(VIRTIO_BLK_T_IN, sector + i, buf + i * SECTOR_SIZE) < 0) {
            return -1;
        }
    }

    return 0;
}

int virtio_blk_write(uint64_t sector, uint32_t count, const void *buffer)
{
    if (!blk_initialized) {
        return -1;
    }

    if (sector + count > blk_capacity) {
        printk(KERN_ERR "VIRTIO_BLK: Write beyond disk capacity\n");
        return -1;
    }

    const uint8_t *buf = (const uint8_t *)buffer;

    for (uint32_t i = 0; i < count; i++) {
        if (do_block_io(VIRTIO_BLK_T_OUT, sector + i, (void *)(buf + i * SECTOR_SIZE)) < 0) {
            return -1;
        }
    }

    return 0;
}

int virtio_blk_flush(void)
{
    if (!blk_initialized) {
        return -1;
    }

    /* For flush, we don't need a data buffer */
    request_hdr.type = VIRTIO_BLK_T_FLUSH;
    request_hdr.reserved = 0;
    request_hdr.sector = 0;

    status_byte = 0xFF;

    /* Setup 2-descriptor chain: header -> status */
    desc_ring[0].addr = (uint64_t)(uintptr_t)&request_hdr;
    desc_ring[0].len = sizeof(virtio_blk_req_t);
    desc_ring[0].flags = VIRTQ_DESC_F_NEXT;
    desc_ring[0].next = 1;

    desc_ring[1].addr = (uint64_t)(uintptr_t)&status_byte;
    desc_ring[1].len = 1;
    desc_ring[1].flags = VIRTQ_DESC_F_WRITE;
    desc_ring[1].next = 0;

    mmio_barrier();

    uint16_t avail_idx = avail_ring.idx;
    avail_ring.ring[avail_idx % QUEUE_SIZE] = 0;
    mmio_barrier();
    avail_ring.idx = avail_idx + 1;
    mmio_barrier();

    mmio_write32(blk_base, VIRTIO_MMIO_QUEUE_NOTIFY, 0);

    int timeout = 10000000;
    while (used_ring.idx == last_used_idx && timeout > 0) {
        mmio_barrier();
        timeout--;
    }

    uint32_t isr = mmio_read32(blk_base, VIRTIO_MMIO_INTERRUPT_STATUS);
    if (isr) {
        mmio_write32(blk_base, VIRTIO_MMIO_INTERRUPT_ACK, isr);
    }

    last_used_idx = used_ring.idx;

    return (status_byte == VIRTIO_BLK_S_OK) ? 0 : -1;
}

/* ===================================================================== */
/* Public API */
/* ===================================================================== */

uint64_t virtio_blk_get_capacity(void)
{
    return blk_capacity;
}

uint32_t virtio_blk_get_sector_size(void)
{
    return blk_sector_size;
}

bool virtio_blk_is_ready(void)
{
    return blk_initialized;
}

/* ===================================================================== */
/* Initialization */
/* ===================================================================== */

int virtio_blk_init(void)
{
    printk(KERN_INFO "VIRTIO_BLK: Initializing VirtIO block driver...\n");

    /* Find VirtIO block device */
    blk_base = find_virtio_blk();
    if (!blk_base) {
        printk(KERN_WARNING "VIRTIO_BLK: No block device found\n");
        return -1;
    }

    /* Reset device */
    mmio_write32(blk_base, VIRTIO_MMIO_STATUS, 0);
    while (mmio_read32(blk_base, VIRTIO_MMIO_STATUS) != 0) {
        asm volatile("nop");
    }

    /* Acknowledge device */
    mmio_write32(blk_base, VIRTIO_MMIO_STATUS, VIRTIO_STATUS_ACK);

    /* Indicate driver loaded */
    mmio_write32(blk_base, VIRTIO_MMIO_STATUS,
                 VIRTIO_STATUS_ACK | VIRTIO_STATUS_DRIVER);

    /* Read device features */
    uint32_t features = mmio_read32(blk_base, VIRTIO_MMIO_DEVICE_FEATURES);
    printk(KERN_INFO "VIRTIO_BLK: Device features: 0x%08x\n", features);

    /* Accept minimal features - don't require any special features */
    mmio_write32(blk_base, VIRTIO_MMIO_DRIVER_FEATURES, 0);

    /* Features OK */
    mmio_write32(blk_base, VIRTIO_MMIO_STATUS,
                 VIRTIO_STATUS_ACK | VIRTIO_STATUS_DRIVER | VIRTIO_STATUS_FEATURES_OK);

    /* Verify features accepted */
    uint32_t status = mmio_read32(blk_base, VIRTIO_MMIO_STATUS);
    if (!(status & VIRTIO_STATUS_FEATURES_OK)) {
        printk(KERN_ERR "VIRTIO_BLK: Feature negotiation failed\n");
        mmio_write32(blk_base, VIRTIO_MMIO_STATUS, VIRTIO_STATUS_FAILED);
        return -1;
    }

    /* Setup virtqueue 0 */
    mmio_write32(blk_base, VIRTIO_MMIO_QUEUE_SEL, 0);

    uint32_t max_queue = mmio_read32(blk_base, VIRTIO_MMIO_QUEUE_NUM_MAX);
    if (max_queue == 0) {
        printk(KERN_ERR "VIRTIO_BLK: Queue not available\n");
        return -1;
    }

    /* Use small queue size */
    uint32_t queue_size = (max_queue < QUEUE_SIZE) ? max_queue : QUEUE_SIZE;
    mmio_write32(blk_base, VIRTIO_MMIO_QUEUE_NUM, queue_size);

    /* Initialize queue structures */
    for (int i = 0; i < QUEUE_SIZE; i++) {
        desc_ring[i].addr = 0;
        desc_ring[i].len = 0;
        desc_ring[i].flags = 0;
        desc_ring[i].next = 0;
    }
    avail_ring.flags = 0;
    avail_ring.idx = 0;
    used_ring.flags = 0;
    used_ring.idx = 0;
    last_used_idx = 0;

    /* Set queue addresses */
    uint64_t desc_addr = (uint64_t)(uintptr_t)desc_ring;
    uint64_t avail_addr = (uint64_t)(uintptr_t)&avail_ring;
    uint64_t used_addr = (uint64_t)(uintptr_t)&used_ring;

    mmio_write32(blk_base, VIRTIO_MMIO_QUEUE_DESC_LOW, (uint32_t)desc_addr);
    mmio_write32(blk_base, VIRTIO_MMIO_QUEUE_DESC_HIGH, (uint32_t)(desc_addr >> 32));
    mmio_write32(blk_base, VIRTIO_MMIO_QUEUE_AVAIL_LOW, (uint32_t)avail_addr);
    mmio_write32(blk_base, VIRTIO_MMIO_QUEUE_AVAIL_HIGH, (uint32_t)(avail_addr >> 32));
    mmio_write32(blk_base, VIRTIO_MMIO_QUEUE_USED_LOW, (uint32_t)used_addr);
    mmio_write32(blk_base, VIRTIO_MMIO_QUEUE_USED_HIGH, (uint32_t)(used_addr >> 32));

    /* Queue ready */
    mmio_write32(blk_base, VIRTIO_MMIO_QUEUE_READY, 1);

    /* Read disk configuration */
    blk_capacity = mmio_read64(blk_base, VIRTIO_MMIO_CONFIG);
    blk_sector_size = 512;  /* Default, could read from config if feature enabled */

    printk(KERN_INFO "VIRTIO_BLK: Capacity: %llu sectors (%llu MB)\n",
           (unsigned long long)blk_capacity,
           (unsigned long long)(blk_capacity * blk_sector_size / (1024 * 1024)));
    printk(KERN_INFO "VIRTIO_BLK: Sector size: %u bytes\n", blk_sector_size);

    /* Driver OK */
    mmio_write32(blk_base, VIRTIO_MMIO_STATUS,
                 VIRTIO_STATUS_ACK | VIRTIO_STATUS_DRIVER |
                 VIRTIO_STATUS_FEATURES_OK | VIRTIO_STATUS_DRIVER_OK);

    /* Verify no failure */
    status = mmio_read32(blk_base, VIRTIO_MMIO_STATUS);
    if (status & VIRTIO_STATUS_FAILED) {
        printk(KERN_ERR "VIRTIO_BLK: Device initialization failed\n");
        return -1;
    }

    blk_initialized = true;
    printk(KERN_INFO "VIRTIO_BLK: Block device initialized successfully\n");

    /* Test read sector 0 to verify driver works */
    uint8_t test_buf[512];
    if (virtio_blk_read(0, 1, test_buf) == 0) {
        printk(KERN_INFO "VIRTIO_BLK: Test read successful\n");
    } else {
        printk(KERN_WARNING "VIRTIO_BLK: Test read failed\n");
    }

    return 0;
}
