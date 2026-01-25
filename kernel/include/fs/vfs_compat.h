/*
 * Vib-OS VFS Compatibility Layer (VibeOS-style API shim)
 *
 * Provides simple VibeOS-compatible VFS functions on top of Vib-OS's
 * Linux-like VFS. This enables ported VibeOS apps to work.
 */

#ifndef VFS_COMPAT_H
#define VFS_COMPAT_H

#include "../include/types.h"

/* Simple VibeOS-style VFS node (wraps our struct file) */
typedef struct vfs_node {
    char name[256];
    size_t size;
    int is_dir;
    void *internal;  /* Pointer to underlying struct file or dentry */
} vfs_node_t;

/* Simple VFS functions for VibeOS compatibility */

/* Look up a file by path, returns vfs_node_t* or NULL */
vfs_node_t *vfs_lookup(const char *path);

/* Open a file and get a handle (like vfs_lookup but allocates) */
vfs_node_t *vfs_open_handle(const char *path);

/* Close and free a handle */
void vfs_close_handle(vfs_node_t *node);

/* Read from a file at offset */
int vfs_read_compat(vfs_node_t *node, char *buf, size_t size, size_t offset);

/* Write to a file */
int vfs_write_compat(vfs_node_t *node, const char *buf, size_t size);

/* Check if node is a directory */
int vfs_is_dir(vfs_node_t *node);

/* Create a file */
vfs_node_t *vfs_create(const char *path);

/* Create a directory */
vfs_node_t *vfs_mkdir_compat(const char *path);

/* Delete a file */
int vfs_delete(const char *path);

/* Delete an empty directory */
int vfs_delete_dir(const char *path);

/* Delete recursively */
int vfs_delete_recursive(const char *path);

/* Rename a file */
int vfs_rename_compat(const char *oldpath, const char *newname);

/* Read directory entry */
int vfs_readdir_compat(vfs_node_t *dir, int index, char *name, size_t name_size, uint8_t *type);

/* Set/get current working directory */
int vfs_set_cwd(const char *path);
int vfs_get_cwd_path(char *buf, size_t size);

#endif /* VFS_COMPAT_H */
