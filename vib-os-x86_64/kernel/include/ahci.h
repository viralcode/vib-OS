/*
 * AHCI SATA controller support for the x86_64 kernel.
 */
#ifndef VIBOS_AHCI_H
#define VIBOS_AHCI_H

/* Discover AHCI HBAs, initialize supported SATA ports and register disks. */
int ahci_init(void);

#endif
