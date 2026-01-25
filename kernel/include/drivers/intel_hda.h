/*
 * Vib-OS - Intel HDA Audio Driver
 */

#ifndef DRIVERS_INTEL_HDA_H
#define DRIVERS_INTEL_HDA_H

#include "types.h"
#include "drivers/pci.h"

/* HDA Register Offsets */
#define HDA_GCAP        0x00    /* Global Capabilities */
#define HDA_VMIN        0x02    /* Minor Version */
#define HDA_VMAJ        0x03    /* Major Version */
#define HDA_OUTPAY      0x04    /* Output Payload Capability */
#define HDA_INPAY       0x06    /* Input Payload Capability */
#define HDA_GCTL        0x08    /* Global Control */
#define HDA_WAKEEN      0x0C    /* Wake Enable */
#define HDA_STATESTS    0x0E    /* State Change Status */
#define HDA_GSTS        0x10    /* Global Status */
#define HDA_INTCTL      0x20    /* Interrupt Control */
#define HDA_INTSTS      0x24    /* Interrupt Status */
#define HDA_WALCLK      0x30    /* Wall Clock Counter */
#define HDA_SSYNC       0x38    /* Stream Synchronization */

#define HDA_CORBLBASE   0x40    /* CORB Lower Base */
#define HDA_CORBUBASE   0x44    /* CORB Upper Base */
#define HDA_CORBWP      0x48    /* CORB Write Pointer */
#define HDA_CORBRP      0x4A    /* CORB Read Pointer */
#define HDA_CORBCTL     0x4C    /* CORB Control */
#define HDA_CORBSTS     0x4D    /* CORB Status */
#define HDA_CORBSIZE    0x4E    /* CORB Size */

#define HDA_RIRBLBASE   0x50    /* RIRB Lower Base */
#define HDA_RIRBUBASE   0x54    /* RIRB Upper Base */
#define HDA_RIRBWP      0x58    /* RIRB Write Pointer */
#define HDA_RINTCNT     0x5A    /* RIRB Response Interrupt Count */
#define HDA_RIRBCTL     0x5C    /* RIRB Control */
#define HDA_RIRBSTS     0x5D    /* RIRB Status */
#define HDA_RIRBSIZE    0x5E    /* RIRB Size */

#define HDA_ICOI        0x60    /* Immediate Command Output Interface */
#define HDA_ICII        0x64    /* Immediate Command Input Interface */
#define HDA_ICIS        0x68    /* Immediate Command Status */

#define HDA_DPLBASE     0x70    /* DMA Position Lower Base */
#define HDA_DPUBASE     0x74    /* DMA Position Upper Base */

/* Stream Descriptor Offsets (Stating at 0x80) */
/* Stream Descriptor Offsets (Relative to Stream Base) */
#define HDA_SD_CTL      0x00
#define HDA_SD_STS      0x03
#define HDA_SD_LPIB     0x04
#define HDA_SD_CBL      0x08
#define HDA_SD_LVI      0x0C
#define HDA_SD_FMT      0x12
#define HDA_SD_BDLPL    0x18
#define HDA_SD_BDLPU    0x1C

/* Stream Control Bits */
#define HDA_SD_CTL_RUN  0x02
#define HDA_SD_CTL_IOCE 0x04
#define HDA_SD_CTL_FEIE 0x08
#define HDA_SD_CTL_DEIE 0x10

/* Buffer Descriptor List Entry */
typedef struct {
    uint64_t addr;
    uint32_t len;
    uint32_t flags; /* Bit 0: IOC */
} __attribute__((packed)) hda_bdl_entry_t;

/* PCI ID */
#define HDA_VENDOR_ID   0x8086
#define HDA_DEVICE_ID   0x2668  /* ICH6 */

void intel_hda_init(pci_device_t *pci_dev);
int intel_hda_play_pcm(const void *data, uint32_t samples, uint8_t channels, uint32_t sample_rate);

#endif
