/*
 * Vib-OS - Raspberry Pi 4/5 Support
 * 
 * BCM2711 (Pi 4) and BCM2712 (Pi 5) hardware support.
 */

#include "printk.h"
#include "types.h"
#include "mm/vmm.h"

/* ===================================================================== */
/* BCM2711/BCM2712 Base Addresses */
/* ===================================================================== */

/* Raspberry Pi 4 (BCM2711) */
#define BCM2711_PERI_BASE       0xFE000000UL
#define BCM2711_GPIO_BASE       (BCM2711_PERI_BASE + 0x200000)
#define BCM2711_UART0_BASE      (BCM2711_PERI_BASE + 0x201000)
#define BCM2711_UART2_BASE      (BCM2711_PERI_BASE + 0x201400)
#define BCM2711_MBOX_BASE       (BCM2711_PERI_BASE + 0x00B880)
#define BCM2711_EMMC2_BASE      (BCM2711_PERI_BASE + 0x340000)
#define BCM2711_USB_BASE        (BCM2711_PERI_BASE + 0x980000)
#define BCM2711_GIC_BASE        0xFF840000UL

/* Raspberry Pi 5 (BCM2712) */
#define BCM2712_PERI_BASE       0x1F00000000UL
#define BCM2712_GPIO_BASE       (BCM2712_PERI_BASE + 0x200000)
#define BCM2712_UART0_BASE      (BCM2712_PERI_BASE + 0x201000)
#define BCM2712_GIC_BASE        0x107FFF9000UL

/* ===================================================================== */
/* GPIO Registers */
/* ===================================================================== */

#define GPIO_FSEL0      0x00
#define GPIO_FSEL1      0x04
#define GPIO_FSEL2      0x08
#define GPIO_SET0       0x1C
#define GPIO_CLR0       0x28
#define GPIO_LEV0       0x34
#define GPIO_PUP_PDN0   0xE4

/* GPIO function select values */
#define GPIO_FUNC_INPUT     0
#define GPIO_FUNC_OUTPUT    1
#define GPIO_FUNC_ALT0      4
#define GPIO_FUNC_ALT1      5
#define GPIO_FUNC_ALT2      6
#define GPIO_FUNC_ALT3      7
#define GPIO_FUNC_ALT4      3
#define GPIO_FUNC_ALT5      2

/* ===================================================================== */
/* Mini UART (for console) */
/* ===================================================================== */

#define AUX_BASE            (BCM2711_PERI_BASE + 0x215000)
#define AUX_ENABLES         0x04
#define AUX_MU_IO           0x40
#define AUX_MU_IER          0x44
#define AUX_MU_IIR          0x48
#define AUX_MU_LCR          0x4C
#define AUX_MU_MCR          0x50
#define AUX_MU_LSR          0x54
#define AUX_MU_CNTL         0x60
#define AUX_MU_BAUD         0x68

/* ===================================================================== */
/* Mailbox Interface (for GPU communication) */
/* ===================================================================== */

#define MBOX_READ       0x00
#define MBOX_STATUS     0x18
#define MBOX_WRITE      0x20

#define MBOX_FULL       0x80000000
#define MBOX_EMPTY      0x40000000

#define MBOX_CH_POWER   0
#define MBOX_CH_FB      1
#define MBOX_CH_VUART   2
#define MBOX_CH_VCHIQ   3
#define MBOX_CH_LED     4
#define MBOX_CH_BTN     5
#define MBOX_CH_TOUCH   6
#define MBOX_CH_PROP    8

/* Property tags */
#define MBOX_TAG_GETSERIAL      0x00010004
#define MBOX_TAG_GETMODEL       0x00010001
#define MBOX_TAG_GETREVISION    0x00010002
#define MBOX_TAG_GETMEMORY      0x00010005
#define MBOX_TAG_SETPOWER       0x00028001
#define MBOX_TAG_GETCLKRATE     0x00030002
#define MBOX_TAG_FB_ALLOC       0x00040001
#define MBOX_TAG_FB_RELEASE     0x00048001
#define MBOX_TAG_FB_SETPHYS     0x00048003
#define MBOX_TAG_FB_SETVIRT     0x00048004
#define MBOX_TAG_FB_SETDEPTH    0x00048005
#define MBOX_TAG_FB_GETPITCH    0x00040008
#define MBOX_TAG_END            0

/* ===================================================================== */
/* Driver State */
/* ===================================================================== */

static volatile uint32_t *gpio_regs = NULL;
static volatile uint32_t *mbox_regs = NULL;
static volatile uint32_t *aux_regs = NULL;

static int rpi_model = 0;  /* 4 or 5 */

/* Mailbox buffer (16-byte aligned) */
static uint32_t mbox_buffer[64] __attribute__((aligned(16)));

/* ===================================================================== */
/* GPIO Functions */
/* ===================================================================== */

static void gpio_set_function(int pin, int func)
{
    if (!gpio_regs || pin < 0 || pin > 57) return;
    
    int reg = pin / 10;
    int shift = (pin % 10) * 3;
    
    uint32_t val = gpio_regs[reg];
    val &= ~(7 << shift);
    val |= (func << shift);
    gpio_regs[reg] = val;
}

static void gpio_set_output(int pin, bool high)
{
    if (!gpio_regs || pin < 0 || pin > 57) return;
    
    int reg = pin / 32;
    int bit = pin % 32;
    
    if (high) {
        gpio_regs[GPIO_SET0/4 + reg] = (1 << bit);
    } else {
        gpio_regs[GPIO_CLR0/4 + reg] = (1 << bit);
    }
}

static bool gpio_get_input(int pin)
{
    if (!gpio_regs || pin < 0 || pin > 57) return false;
    
    int reg = pin / 32;
    int bit = pin % 32;
    
    return (gpio_regs[GPIO_LEV0/4 + reg] & (1 << bit)) != 0;
}

/* ===================================================================== */
/* Mailbox Functions */
/* ===================================================================== */

static int mbox_call(uint8_t channel)
{
    if (!mbox_regs) return -1;
    
    /* Get physical address of buffer */
    uint32_t addr = (uint32_t)(uint64_t)mbox_buffer;
    
    /* Wait for mailbox to be not full */
    while (mbox_regs[MBOX_STATUS/4] & MBOX_FULL);
    
    /* Write address + channel */
    mbox_regs[MBOX_WRITE/4] = (addr & ~0xF) | (channel & 0xF);
    
    /* Wait for response */
    while (1) {
        while (mbox_regs[MBOX_STATUS/4] & MBOX_EMPTY);
        
        uint32_t resp = mbox_regs[MBOX_READ/4];
        if ((resp & 0xF) == channel) {
            return (mbox_buffer[1] == 0x80000000) ? 0 : -1;
        }
    }
}

static int mbox_get_property(uint32_t tag, uint32_t *value, size_t value_len)
{
    size_t idx = 0;
    
    mbox_buffer[idx++] = 0;  /* Size - filled later */
    mbox_buffer[idx++] = 0;  /* Request code */
    
    mbox_buffer[idx++] = tag;
    mbox_buffer[idx++] = value_len;
    mbox_buffer[idx++] = 0;  /* Request indicator */
    
    for (size_t i = 0; i < value_len/4; i++) {
        mbox_buffer[idx++] = value ? value[i] : 0;
    }
    
    mbox_buffer[idx++] = MBOX_TAG_END;
    mbox_buffer[0] = idx * 4;
    
    if (mbox_call(MBOX_CH_PROP) < 0) {
        return -1;
    }
    
    if (value) {
        for (size_t i = 0; i < value_len/4; i++) {
            value[i] = mbox_buffer[5 + i];
        }
    }
    
    return 0;
}

/* ===================================================================== */
/* Mini UART Functions */
/* ===================================================================== */

static void rpi_uart_init(void)
{
    if (!aux_regs) return;
    
    /* Enable mini UART */
    aux_regs[AUX_ENABLES/4] = 1;
    
    /* Disable TX/RX */
    aux_regs[AUX_MU_CNTL/4] = 0;
    
    /* 8-bit mode */
    aux_regs[AUX_MU_LCR/4] = 3;
    
    /* RTS high */
    aux_regs[AUX_MU_MCR/4] = 0;
    
    /* Disable interrupts */
    aux_regs[AUX_MU_IER/4] = 0;
    
    /* Clear FIFOs */
    aux_regs[AUX_MU_IIR/4] = 0xC6;
    
    /* Baud rate = 115200 (assuming 500MHz clock) */
    aux_regs[AUX_MU_BAUD/4] = 270;
    
    /* Set GPIO 14/15 to ALT5 for mini UART */
    gpio_set_function(14, GPIO_FUNC_ALT5);
    gpio_set_function(15, GPIO_FUNC_ALT5);
    
    /* Enable TX/RX */
    aux_regs[AUX_MU_CNTL/4] = 3;
}

void rpi_uart_putc(char c)
{
    if (!aux_regs) return;
    
    /* Wait for TX ready */
    while (!(aux_regs[AUX_MU_LSR/4] & 0x20));
    
    aux_regs[AUX_MU_IO/4] = c;
}

char rpi_uart_getc(void)
{
    if (!aux_regs) return 0;
    
    /* Wait for RX ready */
    while (!(aux_regs[AUX_MU_LSR/4] & 0x01));
    
    return aux_regs[AUX_MU_IO/4] & 0xFF;
}

/* ===================================================================== */
/* Framebuffer */
/* ===================================================================== */

struct rpi_fb_info {
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint32_t depth;
    void *buffer;
    size_t size;
};

static struct rpi_fb_info rpi_fb = {0};

int rpi_fb_init(uint32_t width, uint32_t height, uint32_t depth)
{
    printk(KERN_INFO "RPI: Initializing framebuffer %ux%u@%u\n", 
           width, height, depth);
    
    size_t idx = 0;
    
    mbox_buffer[idx++] = 0;  /* Size */
    mbox_buffer[idx++] = 0;  /* Request */
    
    /* Set physical display size */
    mbox_buffer[idx++] = MBOX_TAG_FB_SETPHYS;
    mbox_buffer[idx++] = 8;
    mbox_buffer[idx++] = 0;
    mbox_buffer[idx++] = width;
    mbox_buffer[idx++] = height;
    
    /* Set virtual display size */
    mbox_buffer[idx++] = MBOX_TAG_FB_SETVIRT;
    mbox_buffer[idx++] = 8;
    mbox_buffer[idx++] = 0;
    mbox_buffer[idx++] = width;
    mbox_buffer[idx++] = height;
    
    /* Set depth */
    mbox_buffer[idx++] = MBOX_TAG_FB_SETDEPTH;
    mbox_buffer[idx++] = 4;
    mbox_buffer[idx++] = 0;
    mbox_buffer[idx++] = depth;
    
    /* Allocate framebuffer */
    mbox_buffer[idx++] = MBOX_TAG_FB_ALLOC;
    mbox_buffer[idx++] = 8;
    mbox_buffer[idx++] = 0;
    mbox_buffer[idx++] = 16;  /* Alignment */
    mbox_buffer[idx++] = 0;   /* Size returned here */
    
    /* Get pitch */
    mbox_buffer[idx++] = MBOX_TAG_FB_GETPITCH;
    mbox_buffer[idx++] = 4;
    mbox_buffer[idx++] = 0;
    mbox_buffer[idx++] = 0;
    
    mbox_buffer[idx++] = MBOX_TAG_END;
    mbox_buffer[0] = idx * 4;
    
    if (mbox_call(MBOX_CH_PROP) < 0) {
        printk(KERN_ERR "RPI: Framebuffer allocation failed\n");
        return -1;
    }
    
    /* Parse response */
    rpi_fb.width = width;
    rpi_fb.height = height;
    rpi_fb.depth = depth;
    
    /* Find FB_ALLOC response */
    for (size_t i = 2; i < idx; i++) {
        if (mbox_buffer[i] == MBOX_TAG_FB_ALLOC) {
            rpi_fb.buffer = (void *)(uint64_t)(mbox_buffer[i+3] & 0x3FFFFFFF);
            rpi_fb.size = mbox_buffer[i+4];
            break;
        }
    }
    
    /* Find pitch */
    for (size_t i = 2; i < idx; i++) {
        if (mbox_buffer[i] == MBOX_TAG_FB_GETPITCH) {
            rpi_fb.pitch = mbox_buffer[i+3];
            break;
        }
    }
    
    if (!rpi_fb.buffer) {
        printk(KERN_ERR "RPI: No framebuffer address\n");
        return -1;
    }
    
    printk(KERN_INFO "RPI: Framebuffer at %p, size %zu, pitch %u\n",
           rpi_fb.buffer, rpi_fb.size, rpi_fb.pitch);
    
    return 0;
}

void *rpi_fb_get_buffer(void)
{
    return rpi_fb.buffer;
}

/* ===================================================================== */
/* Initialization */
/* ===================================================================== */

int rpi_init(void)
{
    printk(KERN_INFO "RPI: Detecting Raspberry Pi model...\n");
    
    /* Try Pi 4 first */
    gpio_regs = (volatile uint32_t *)BCM2711_GPIO_BASE;
    mbox_regs = (volatile uint32_t *)BCM2711_MBOX_BASE;
    aux_regs = (volatile uint32_t *)AUX_BASE;
    
    /* Map MMIO regions */
    vmm_map_range(BCM2711_PERI_BASE, BCM2711_PERI_BASE, 0x1000000, VM_DEVICE);
    
    /* Get board revision */
    uint32_t revision = 0;
    if (mbox_get_property(MBOX_TAG_GETREVISION, &revision, 4) == 0) {
        if ((revision & 0xFFFFFF) >= 0xC03111) {
            rpi_model = 4;
            printk(KERN_INFO "RPI: Raspberry Pi 4 detected (rev 0x%x)\n", revision);
        }
    }
    
    if (rpi_model == 0) {
        /* Could be Pi 5 or unsupported */
        printk(KERN_WARNING "RPI: Unknown model, assuming Pi 4 compatible\n");
        rpi_model = 4;
    }
    
    /* Initialize UART */
    rpi_uart_init();
    printk(KERN_INFO "RPI: UART initialized\n");
    
    /* Initialize framebuffer */
    rpi_fb_init(1920, 1080, 32);
    
    printk(KERN_INFO "RPI: Initialization complete\n");
    
    return 0;
}
