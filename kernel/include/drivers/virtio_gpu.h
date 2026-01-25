/*
 * Vib-OS - virtio-gpu Driver Header
 */

#ifndef DRIVERS_VIRTIO_GPU_H
#define DRIVERS_VIRTIO_GPU_H

#include "drivers/pci.h"
#include "types.h"

/* Initialize virtio-gpu device */
int virtio_gpu_init(pci_device_t *pci);

/* Check if virtio-gpu is available and initialized */
bool virtio_gpu_is_available(void);

/* Check if 3D (virgl) acceleration is available */
bool virtio_gpu_has_3d(void);

/* Get display size */
void virtio_gpu_get_display_size(uint32_t *width, uint32_t *height);

#endif
