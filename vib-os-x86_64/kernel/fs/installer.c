#include "../include/installer.h"
#include "../include/blockdev.h"
#include "../include/fat32.h"
#include "../include/gpt.h"
#include "../include/kmalloc.h"
#include "../include/string.h"

#define INSTALLER_MAX_KERNEL_SIZE (16U * 1024U * 1024U)
#define INSTALLER_MAX_LOADER_SIZE (2U * 1024U * 1024U)
#define INSTALLER_MAX_CONFIG_SIZE (64U * 1024U)
#define INSTALLER_MIN_DISK_SECTORS 70000ULL

#define TAG_KERNEL "vibos-install-kernel"
#define TAG_LOADER "vibos-install-uefi-loader"
#define TAG_CONFIG "vibos-install-config"

typedef struct {
  const uint8_t *data;
  uint32_t size;
} installer_asset_t;

static installer_asset_t kernel_asset;
static installer_asset_t loader_asset;
static installer_asset_t config_asset;

static int register_asset(installer_asset_t *asset, const void *data,
                          uint64_t size, uint32_t max_size) {
  if (!asset || !data || size == 0 || size > max_size || size > 0xFFFFFFFFULL)
    return INSTALLER_ERR_PAYLOAD;
  if (asset->data) return INSTALLER_ERR_PAYLOAD;
  asset->data = (const uint8_t *)data;
  asset->size = (uint32_t)size;
  return INSTALLER_OK;
}

int installer_register_payload(const char *tag, const void *data,
                               uint64_t size) {
  if (!tag || !data || size == 0) return INSTALLER_ERR_PAYLOAD;
  if (strcmp(tag, TAG_KERNEL) == 0)
    return register_asset(&kernel_asset, data, size, INSTALLER_MAX_KERNEL_SIZE);
  if (strcmp(tag, TAG_LOADER) == 0)
    return register_asset(&loader_asset, data, size, INSTALLER_MAX_LOADER_SIZE);
  if (strcmp(tag, TAG_CONFIG) == 0)
    return register_asset(&config_asset, data, size, INSTALLER_MAX_CONFIG_SIZE);
  return INSTALLER_ERR_PAYLOAD;
}

int installer_payload_ready(void) {
  return kernel_asset.data && kernel_asset.size && loader_asset.data &&
         loader_asset.size && config_asset.data && config_asset.size;
}

static int copy_and_verify(fat32_volume_t *volume, const char *path,
                           const installer_asset_t *asset) {
  uint8_t *verify;
  uint32_t actual_size = 0;
  int status;
  if (!volume || !path || !asset || !asset->data || !asset->size)
    return INSTALLER_ERR_PAYLOAD;

  status = fat32_write_file(volume, path, asset->data, asset->size);
  if (status != FAT32_OK) return INSTALLER_ERR_WRITE;

  verify = (uint8_t *)kmalloc(asset->size);
  if (!verify) return INSTALLER_ERR_MEMORY;
  status = fat32_read_file(volume, path, verify, asset->size, &actual_size);
  if (status != FAT32_OK || actual_size != asset->size ||
      memcmp(verify, asset->data, asset->size) != 0) {
    kfree(verify);
    return INSTALLER_ERR_VERIFY;
  }
  kfree(verify);
  return INSTALLER_OK;
}

static void report_progress(installer_progress_fn progress, const char *phase) {
  if (progress) progress(phase);
}

int installer_install_with_progress(uint32_t disk_index,
                                    installer_progress_fn progress) {
  blockdev_t *device;
  gpt_partition_t esp;
  fat32_volume_t volume;
  int status;

  if (!installer_payload_ready()) return INSTALLER_ERR_PAYLOAD;
  device = blockdev_get(disk_index);
  if (!device || !device->read || !device->write)
    return INSTALLER_ERR_DEVICE;
  if (device->sector_size != 512 ||
      device->sector_count < INSTALLER_MIN_DISK_SECTORS)
    return INSTALLER_ERR_GEOMETRY;

  report_progress(progress, "Writing GPT partition table...");
  status = gpt_create_single_esp(device, &esp);
  if (status != GPT_OK) return INSTALLER_ERR_GPT;
  report_progress(progress, "Formatting the FAT32 EFI System Partition...");
  status = fat32_format(device, esp.first_lba,
                        esp.last_lba - esp.first_lba + 1ULL, "VIBOS");
  if (status != FAT32_OK) return INSTALLER_ERR_FORMAT;
  report_progress(progress, "Mounting and validating the new FAT32 volume...");
  status = fat32_mount(device, esp.first_lba,
                       esp.last_lba - esp.first_lba + 1ULL, &volume);
  if (status != FAT32_OK) return INSTALLER_ERR_FILESYSTEM;

  report_progress(progress, "Creating EFI and boot directories...");
  if (fat32_mkdir(&volume, "EFI") != FAT32_OK ||
      fat32_mkdir(&volume, "EFI/BOOT") != FAT32_OK ||
      fat32_mkdir(&volume, "boot") != FAT32_OK ||
      fat32_mkdir(&volume, "limine") != FAT32_OK)
    return INSTALLER_ERR_FILESYSTEM;

  /* Limine's x86-64 UEFI fallback path and the configured kernel path. */
  report_progress(progress, "Copying the UEFI fallback boot loader...");
  status = copy_and_verify(&volume, "EFI/BOOT/BOOTX64.EFI", &loader_asset);
  if (status != INSTALLER_OK) return status;
  report_progress(progress, "Copying the installed Vib-OS kernel...");
  status = copy_and_verify(&volume, "boot/kernel.elf", &kernel_asset);
  if (status != INSTALLER_OK) return status;

  /* Limine searches several conventional config locations; install identical
   * copies. VFAT LFN support is required for the four-character .conf suffix.
   */
  report_progress(progress, "Writing and verifying the installed boot configuration...");
  status = copy_and_verify(&volume, "limine.conf", &config_asset);
  if (status != INSTALLER_OK) return status;
  status = copy_and_verify(&volume, "boot/limine.conf", &config_asset);
  if (status != INSTALLER_OK) return status;
  status = copy_and_verify(&volume, "limine/limine.conf", &config_asset);
  if (status != INSTALLER_OK) return status;
  status = copy_and_verify(&volume, "EFI/BOOT/limine.conf", &config_asset);
  if (status != INSTALLER_OK) return status;

  report_progress(progress, "Flushing completed disk writes...");
  if (blockdev_flush(device) != 0) return INSTALLER_ERR_FLUSH;
  return INSTALLER_OK;
}

int installer_install(uint32_t disk_index) {
  return installer_install_with_progress(disk_index, NULL);
}

const char *installer_error_string(int status) {
  switch (status) {
  case INSTALLER_OK: return "Installation completed and file read-back verified.";
  case INSTALLER_ERR_PAYLOAD: return "Boot payload unavailable or invalid; no installation started.";
  case INSTALLER_ERR_DEVICE: return "Selected disk is unavailable or not writable.";
  case INSTALLER_ERR_GEOMETRY: return "Disk is too small or not supported (512-byte sectors required).";
  case INSTALLER_ERR_GPT: return "GPT creation failed; disk may be partially changed.";
  case INSTALLER_ERR_FORMAT: return "FAT32 format failed after repartitioning; disk is not bootable.";
  case INSTALLER_ERR_FILESYSTEM: return "FAT32 mount or directory creation failed; disk is not bootable.";
  case INSTALLER_ERR_WRITE: return "A boot file could not be written; disk is not bootable.";
  case INSTALLER_ERR_VERIFY: return "Boot-file read-back verification failed; disk is not bootable.";
  case INSTALLER_ERR_FLUSH: return "Final disk flush failed; installation durability is uncertain.";
  case INSTALLER_ERR_MEMORY: return "Not enough memory to verify a copied boot file.";
  default: return "Unknown installer error.";
  }
}
