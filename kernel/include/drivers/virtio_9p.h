#ifndef _VIRTIO_9P_H
#define _VIRTIO_9P_H

#include "types.h"

/*
 * Minimal virtio-9p client (read-only-ish).
 *
 * Provides host file access for development workflows (QEMU shared folder).
 * Transport: virtio-mmio, single request queue.
 *
 * This is NOT a full VFS filesystem driver; it's a helper API intended for
 * loading files into memory (e.g. video player tests) without rebuilding the OS.
 */

int virtio_9p_init(void);
int virtio_9p_is_ready(void);

/* Best-effort diagnostics (static strings). */
const char *virtio_9p_get_tag(void);
const char *virtio_9p_get_last_error(void);

/*
 * Read a host file (path relative to the shared folder root) into a freshly
 * allocated buffer. Caller owns the returned buffer and must kfree() it.
 *
 * max_bytes is a safety limit.
 */
int virtio_9p_read_file(const char *path, uint8_t **out_data, size_t *out_size,
                        size_t max_bytes);

/*
 * List a host directory (path relative to the shared folder root).
 *
 * Uses the same callback signature as vfs_readdir() for easy UI integration.
 * The callback's "type" matches vfs (mode >> 12): 4=dir, 8=regular file.
 */
int virtio_9p_readdir(const char *path, void *ctx,
                      int (*filldir)(void *, const char *, int, loff_t, ino_t,
                                     unsigned));

#endif /* _VIRTIO_9P_H */
