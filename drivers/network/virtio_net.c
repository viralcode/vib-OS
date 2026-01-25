/*
 * Vib-OS - Virtio MMIO Network Driver
 */

#include "types.h"
#include "printk.h"
#include "mm/kmalloc.h"
#include "net/net.h"

/* String helpers */
void *memset(void *s, int c, size_t n);
void *memcpy(void *dest, const void *src, size_t n);

/* ===================================================================== */
/* Virtio MMIO Definitions */
/* ===================================================================== */

#define VIRTIO_MMIO_BASE        0x0a000000
#define VIRTIO_MMIO_STRIDE      0x200

#define VIRTIO_MMIO_MAGIC           0x000
#define VIRTIO_MMIO_VERSION         0x004
#define VIRTIO_MMIO_DEVICE_ID       0x008
#define VIRTIO_MMIO_VENDOR_ID       0x00c
#define VIRTIO_MMIO_DEVICE_FEATURES 0x010
#define VIRTIO_MMIO_DRIVER_FEATURES 0x020
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
#define VIRTIO_MMIO_CONFIG_GENERATION 0x0fc
#define VIRTIO_MMIO_CONFIG          0x100

#define VIRTIO_STATUS_ACK       1
#define VIRTIO_STATUS_DRIVER    2
#define VIRTIO_STATUS_DRIVER_OK 4
#define VIRTIO_STATUS_FEATURES_OK 8

#define VIRTIO_DEV_NET          1

/* Net Features */
#define VIRTIO_NET_F_MAC        (1 << 5)
#define VIRTIO_NET_F_STATUS     (1 << 16)

/* Virtqueues */
#define VQ_RX 0
#define VQ_TX 1
#define VIRT_QUEUE_SIZE 16

/* Virtqueue structures */
typedef struct __attribute__((packed)) {
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
} virtq_desc_t;

typedef struct __attribute__((packed)) {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[VIRT_QUEUE_SIZE];
} virtq_avail_t;

typedef struct __attribute__((packed)) {
    uint32_t id;
    uint32_t len;
} virtq_used_elem_t;

typedef struct __attribute__((packed)) {
    uint16_t flags;
    uint16_t idx;
    virtq_used_elem_t ring[VIRT_QUEUE_SIZE];
} virtq_used_t;

#define DESC_F_NEXT         1
#define DESC_F_WRITE        2

/* Virtio Net Header */
struct virtio_net_hdr {
    uint8_t flags;
    uint8_t gso_type;
    uint16_t hdr_len;
    uint16_t gso_size;
    uint16_t csum_start;
    uint16_t csum_offset;
    /* uint16_t num_buffers; Only if VIRTIO_NET_F_MRG_RXBUF */
} __attribute__((packed));

/* Packets */
#define PACKET_SIZE 1524 /* 1514 + Header */

/* ===================================================================== */
/* State */
/* ===================================================================== */

static volatile uint32_t *net_base = 0;
static struct net_interface *net_iface = 0;

/* Queues */
struct virt_queue {
    uint8_t *mem;
    virtq_desc_t *desc;
    virtq_avail_t *avail;
    virtq_used_t *used;
    uint16_t last_used_idx;
    void *bufs[VIRT_QUEUE_SIZE]; /* Helper to track buffers */
};

static struct virt_queue rx_q;
static struct virt_queue tx_q;

/* ===================================================================== */
/* Helpers */
/* ===================================================================== */

static void mmio_barrier(void) {
#ifdef ARCH_ARM64
    asm volatile("dsb sy" ::: "memory");
#elif defined(ARCH_X86_64) || defined(ARCH_X86)
    asm volatile("mfence" ::: "memory");
#endif
}

static uint32_t mmio_read32(volatile uint32_t *addr) {
    uint32_t val = *addr;
    mmio_barrier();
    return val;
}

static void mmio_write32(volatile uint32_t *addr, uint32_t val) {
    mmio_barrier();
    *addr = val;
    mmio_barrier();
}

/* ===================================================================== */
/* Packet Handling */
/* ===================================================================== */

int virtio_net_send(struct net_interface *iface, const void *data, size_t len)
{
    (void)iface;
    if (!net_base) return -1;
    
    /* Find free descriptor in TX queue */
    /* Checks omitted for brevity - assuming low traffic unique sender */
    
    uint16_t idx = tx_q.avail->idx % VIRT_QUEUE_SIZE;
    uint16_t desc_idx = idx; /* Simple mapping 1:1 */
    
    /* Fill buffer */
    /* Note: In real driver we should use shared buffers not copy */
    struct virtio_net_hdr *hdr = (struct virtio_net_hdr *)tx_q.bufs[desc_idx];
    memset(hdr, 0, sizeof(struct virtio_net_hdr));
    
    uint8_t *pkt_data = (uint8_t *)(hdr + 1);
    memcpy(pkt_data, data, len);
    
    /* Update Descriptor */
    tx_q.desc[desc_idx].len = sizeof(struct virtio_net_hdr) + len;
    tx_q.desc[desc_idx].flags = 0; /* Read-only for device */
    
    /* Add to Avail */
    tx_q.avail->ring[tx_q.avail->idx % VIRT_QUEUE_SIZE] = desc_idx;
    mmio_barrier();
    tx_q.avail->idx++;
    mmio_barrier();
    
    /* Notify */
    mmio_write32(net_base + VIRTIO_MMIO_QUEUE_NOTIFY/4, VQ_TX);
    
    return len;
}

void virtio_net_poll(void)
{
    if (!net_base || !net_iface) return;
    
    /* Check RX Queue Used Ring */
    while (rx_q.last_used_idx != rx_q.used->idx) {
        uint16_t idx = rx_q.last_used_idx % VIRT_QUEUE_SIZE;
        uint32_t id = rx_q.used->ring[idx].id;
        uint32_t len = rx_q.used->ring[idx].len;
        
        struct virtio_net_hdr *hdr = (struct virtio_net_hdr *)rx_q.bufs[id];
        uint8_t *data = (uint8_t *)(hdr + 1);
        
        /* Pass to Stack */
        if (len > sizeof(struct virtio_net_hdr)) {
            net_rx(net_iface, data, len - sizeof(struct virtio_net_hdr));
        }
        
        /* Re-queue buffer */
        rx_q.avail->ring[rx_q.avail->idx % VIRT_QUEUE_SIZE] = id;
        rx_q.avail->idx++;
        
        rx_q.last_used_idx++;
    }
    
    /* Check TX completions (cleanup) - Optional for now */
    
    /* Notify (if added to avail) */
    mmio_write32(net_base + VIRTIO_MMIO_QUEUE_NOTIFY/4, VQ_RX);
}

/* ===================================================================== */
/* Initialization */
/* ===================================================================== */

static volatile uint32_t *find_virtio_net(void) {
    for (int i = 0; i < 32; i++) {
        volatile uint32_t *base = (volatile uint32_t *)(uintptr_t)(VIRTIO_MMIO_BASE + i * VIRTIO_MMIO_STRIDE);
        uint32_t magic = mmio_read32(base + VIRTIO_MMIO_MAGIC/4);
        uint32_t device_id = mmio_read32(base + VIRTIO_MMIO_DEVICE_ID/4);
        
        if (magic == 0x74726976 && device_id == VIRTIO_DEV_NET) {
            return base;
        }
    }
    return 0;
}

static void setup_queue(int qidx, struct virt_queue *q) {
    /* Select Queue */
    mmio_write32(net_base + VIRTIO_MMIO_QUEUE_SEL/4, qidx);
    
    /* Check max size */
    // uint32_t num_max = mmio_read32(net_base + VIRTIO_MMIO_QUEUE_NUM_MAX/4);
    
    mmio_write32(net_base + VIRTIO_MMIO_QUEUE_NUM/4, VIRT_QUEUE_SIZE);
    
    /* Allocate Memory */
    /* 4096 alignment required for queue structs roughly */
    q->mem = kmalloc(4096 * 2); /* Plenty */
    uintptr_t base = (uintptr_t)q->mem;
    base = (base + 4095) & ~4095;
    
    q->desc = (virtq_desc_t *)base;
    q->avail = (virtq_avail_t *)(base + VIRT_QUEUE_SIZE * sizeof(virtq_desc_t));
    q->used = (virtq_used_t *)(base + 2048); /* Simplified offset */
    
    /* Program Address */
    mmio_write32(net_base + VIRTIO_MMIO_QUEUE_DESC_LOW/4, (uint32_t)(uintptr_t)q->desc);
    mmio_write32(net_base + VIRTIO_MMIO_QUEUE_DESC_HIGH/4, (uint32_t)((uint64_t)(uintptr_t)q->desc >> 32));
    mmio_write32(net_base + VIRTIO_MMIO_QUEUE_AVAIL_LOW/4, (uint32_t)(uintptr_t)q->avail);
    mmio_write32(net_base + VIRTIO_MMIO_QUEUE_AVAIL_HIGH/4, (uint32_t)((uint64_t)(uintptr_t)q->avail >> 32));
    mmio_write32(net_base + VIRTIO_MMIO_QUEUE_USED_LOW/4, (uint32_t)(uintptr_t)q->used);
    mmio_write32(net_base + VIRTIO_MMIO_QUEUE_USED_HIGH/4, (uint32_t)((uint64_t)(uintptr_t)q->used >> 32));
    
    mmio_write32(net_base + VIRTIO_MMIO_QUEUE_READY/4, 1);
    
    q->last_used_idx = 0;
    q->avail->idx = 0;
    q->avail->flags = 0;
}

int virtio_net_init(void) {
    printk(KERN_INFO "NET: Initializing virtio-net...\n");
    
    net_base = find_virtio_net();
    if (!net_base) {
        printk(KERN_WARNING "NET: No virtio-net device found\n");
        return -1;
    }
    
    /* Reset */
    mmio_write32(net_base + VIRTIO_MMIO_STATUS/4, 0);
    
    /* ACK + DRIVER */
    mmio_write32(net_base + VIRTIO_MMIO_STATUS/4, VIRTIO_STATUS_ACK | VIRTIO_STATUS_DRIVER);
    
    /* Read MAC (Config offset 0) */
    uint8_t mac[6];
    
    /* Note: MMIO config is after 0x100 */
    /* VIRTIO_MMIO_CONFIG is 0x100 */
    /* Need byte access to config area */
    volatile uint8_t *config_bytes = (volatile uint8_t *)((uintptr_t)net_base + VIRTIO_MMIO_CONFIG);
    
    for(int i=0; i<6; i++) mac[i] = config_bytes[i];
    
    printk(KERN_INFO "NET: MAC %02x:%02x:%02x:%02x:%02x:%02x\n",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
           
    /* FEATURES_OK */
    mmio_write32(net_base + VIRTIO_MMIO_STATUS/4, 
                 VIRTIO_STATUS_ACK | VIRTIO_STATUS_DRIVER | VIRTIO_STATUS_FEATURES_OK);
                 
    /* Setup Queues */
    setup_queue(VQ_RX, &rx_q);
    setup_queue(VQ_TX, &tx_q);
    
    /* Fill RX Queue */
    for(int i=0; i<VIRT_QUEUE_SIZE; i++) {
        rx_q.bufs[i] = kmalloc(PACKET_SIZE);
        rx_q.desc[i].addr = (uint64_t)rx_q.bufs[i];
        rx_q.desc[i].len = PACKET_SIZE;
        rx_q.desc[i].flags = DESC_F_WRITE; /* Device writes to it */
        rx_q.desc[i].next = 0;
        
        rx_q.avail->ring[i] = i;
    }
    rx_q.avail->idx = VIRT_QUEUE_SIZE;
    
    /* Fill TX Queue Headers */
    for(int i=0; i<VIRT_QUEUE_SIZE; i++) {
        tx_q.bufs[i] = kmalloc(PACKET_SIZE);
        tx_q.desc[i].addr = (uint64_t)tx_q.bufs[i];
        // tx_q.desc[i].len set on send
        // tx_q.desc[i].flags set on send
    }
    
    /* DRIVER_OK */
    mmio_write32(net_base + VIRTIO_MMIO_STATUS/4, 
                 VIRTIO_STATUS_ACK | VIRTIO_STATUS_DRIVER | VIRTIO_STATUS_FEATURES_OK | VIRTIO_STATUS_DRIVER_OK);
                 
    /* Register Interface */
    /* Hardcoded IP for now: 10.0.2.15 (QEMU User Net default) */
    net_iface = net_add_interface("eth0", mac, 0x0A00020F, 0xFFFFFF00, 0x0A000202);
    
    if (net_iface) {
        net_iface->send = virtio_net_send;
    }
    
    return 0;
}
