/*
 * Vib-OS UEFI installer interface.
 * Payload bytes are registered from Limine-loaded modules and remain valid
 * for the lifetime of the boot session.
 */
#ifndef VIBOS_INSTALLER_H
#define VIBOS_INSTALLER_H

#include "types.h"

#define INSTALLER_OK                    0
#define INSTALLER_ERR_PAYLOAD          -1
#define INSTALLER_ERR_DEVICE           -2
#define INSTALLER_ERR_GEOMETRY         -3
#define INSTALLER_ERR_GPT              -4
#define INSTALLER_ERR_FORMAT           -5
#define INSTALLER_ERR_FILESYSTEM        -6
#define INSTALLER_ERR_WRITE            -7
#define INSTALLER_ERR_VERIFY           -8
#define INSTALLER_ERR_FLUSH            -9
#define INSTALLER_ERR_MEMORY          -10

/* Register a Limine module by its configured module_string tag. */
int installer_register_payload(const char *tag, const void *data,
                               uint64_t size);
int installer_payload_ready(void);

typedef void (*installer_progress_fn)(const char *phase);

/* Destructively installs a UEFI-bootable GPT/FAT32 system on this disk index.
 * Call only after the user has explicitly confirmed erasing the whole disk.
 */
int installer_install(uint32_t disk_index);
int installer_install_with_progress(uint32_t disk_index,
                                    installer_progress_fn progress);
const char *installer_error_string(int status);

#endif /* VIBOS_INSTALLER_H */
