#ifndef VIBOS_FAT32_H
#define VIBOS_FAT32_H

#include "block.h"

int fat32_mount_first_volume(block_device_t *device);

#endif
