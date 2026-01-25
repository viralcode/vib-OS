/*
 * Vib-OS VFS Compatibility Layer Implementation
 *
 * Provides simple VibeOS-compatible VFS functions.
 * Includes embedded doom binary for /bin/doom access.
 */

#include "../include/fs/vfs_compat.h"
#include "../include/printk.h"
#include "mm/kmalloc.h"

/* Include embedded doom binary */
#include "../apps/doom_binary.h"
#define DOOM_BINARY_DATA user_bin_doom_build_doom
#define DOOM_BINARY_SIZE sizeof(user_bin_doom_build_doom)

/* Include embedded DOOM1.WAD game data */
#include "../apps/doom1_wad.h"
#define DOOM_WAD_DATA doom1_wad
#define DOOM_WAD_SIZE sizeof(doom1_wad)

/* Current working directory */
static char cwd[256] = "/";

/* Static node pool for simplicity */
#define MAX_VFS_NODES 32
static vfs_node_t node_pool[MAX_VFS_NODES];
static int node_used[MAX_VFS_NODES] = {0};

/* Simple string compare */
static int strcmp_simple(const char *s1, const char *s2) {
    while (*s1 && *s1 == *s2) { s1++; s2++; }
    return *s1 - *s2;
}

/* Allocate a node from pool */
static vfs_node_t *alloc_node(void) {
    for (int i = 0; i < MAX_VFS_NODES; i++) {
        if (!node_used[i]) {
            node_used[i] = 1;
            return &node_pool[i];
        }
    }
    return NULL;
}

/* Free a node back to pool */
static void free_node(vfs_node_t *node) {
    for (int i = 0; i < MAX_VFS_NODES; i++) {
        if (&node_pool[i] == node) {
            node_used[i] = 0;
            return;
        }
    }
}

/* Simple string copy */
static void strcpy_safe(char *dst, const char *src, size_t max) {
    size_t i = 0;
    while (src[i] && i < max - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

/* Look up a file by path */
vfs_node_t *vfs_lookup(const char *path) {
    vfs_node_t *node = alloc_node();
    if (!node) return NULL;
    
    strcpy_safe(node->name, path, sizeof(node->name));
    node->size = 0;
    node->is_dir = 0;
    node->internal = NULL;
    
    /* Check for known paths */
    if (path[0] == '/' && path[1] == '\0') {
        node->is_dir = 1;
    }
    /* Check for doom binary */
    else if (strcmp_simple(path, "/bin/doom") == 0) {
        node->size = DOOM_BINARY_SIZE;
        node->internal = (void*)DOOM_BINARY_DATA;
        printk("[VFS] Found /bin/doom (%u bytes)\n", (unsigned)DOOM_BINARY_SIZE);
    }
    /* Check for DOOM1.WAD game data - support multiple paths */
    else if (strcmp_simple(path, "/DOOM1.WAD") == 0 || 
             strcmp_simple(path, "DOOM1.WAD") == 0 ||
             strcmp_simple(path, "/data/DOOM1.WAD") == 0 ||
             strcmp_simple(path, "/games/doom1.wad") == 0 ||
             strcmp_simple(path, "/games/DOOM1.WAD") == 0 ||
             strcmp_simple(path, "games/doom1.wad") == 0 ||
             strcmp_simple(path, "/doom1.wad") == 0 ||
             strcmp_simple(path, "doom1.wad") == 0) {
        node->size = DOOM_WAD_SIZE;
        node->internal = (void*)DOOM_WAD_DATA;
        printk("[VFS] Found DOOM1.WAD at '%s' (%u bytes)\\n", path, (unsigned)DOOM_WAD_SIZE);
    }
    
    return node;
}

/* Open a file handle */
vfs_node_t *vfs_open_handle(const char *path) {
    return vfs_lookup(path);
}

/* Close a handle */
void vfs_close_handle(vfs_node_t *node) {
    if (node) {
        free_node(node);
    }
}

/* Read from file */
int vfs_read_compat(vfs_node_t *node, char *buf, size_t size, size_t offset) {
    if (!node || !buf) return -1;
    
    /* Check if this is the doom binary */
    if (node->internal == (void*)DOOM_BINARY_DATA) {
        size_t avail = node->size > offset ? node->size - offset : 0;
        size_t to_read = size < avail ? size : avail;
        
        if (to_read > 0) {
            const unsigned char *src = DOOM_BINARY_DATA + offset;
            for (size_t i = 0; i < to_read; i++) {
                buf[i] = src[i];
            }
        }
        return (int)to_read;
    }
    
    /* Check if this is DOOM1.WAD */
    if (node->internal == (void*)DOOM_WAD_DATA) {
        size_t avail = node->size > offset ? node->size - offset : 0;
        size_t to_read = size < avail ? size : avail;
        
        if (to_read > 0) {
            const unsigned char *src = DOOM_WAD_DATA + offset;
            for (size_t i = 0; i < to_read; i++) {
                buf[i] = src[i];
            }
        }
        return (int)to_read;
    }
    
    /* No data for other files */
    return 0;
}

/* Write to file */
int vfs_write_compat(vfs_node_t *node, const char *buf, size_t size) {
    (void)node;
    (void)buf;
    (void)size;
    return 0;
}

/* Check if directory */
int vfs_is_dir(vfs_node_t *node) {
    return node ? node->is_dir : 0;
}

/* Create file */
vfs_node_t *vfs_create_compat(const char *path) {
    vfs_node_t *node = alloc_node();
    if (!node) return NULL;
    strcpy_safe(node->name, path, sizeof(node->name));
    node->size = 0;
    node->is_dir = 0;
    node->internal = NULL;
    return node;
}

/* Create directory */
vfs_node_t *vfs_mkdir_compat(const char *path) {
    vfs_node_t *node = alloc_node();
    if (!node) return NULL;
    strcpy_safe(node->name, path, sizeof(node->name));
    node->size = 0;
    node->is_dir = 1;
    node->internal = NULL;
    return node;
}

/* Delete file */
int vfs_delete(const char *path) {
    (void)path;
    return -1;  /* Not implemented */
}

/* Delete directory */
int vfs_delete_dir(const char *path) {
    (void)path;
    return -1;
}

/* Delete recursive */
int vfs_delete_recursive(const char *path) {
    (void)path;
    return -1;
}

/* Rename */
int vfs_rename_compat(const char *oldpath, const char *newname) {
    (void)oldpath;
    (void)newname;
    return -1;
}

/* Read directory */
int vfs_readdir_compat(vfs_node_t *dir, int index, char *name, size_t name_size, uint8_t *type) {
    (void)dir;
    (void)index;
    (void)name;
    (void)name_size;
    (void)type;
    return -1;  /* End of directory */
}

/* Set CWD */
int vfs_set_cwd(const char *path) {
    strcpy_safe(cwd, path, sizeof(cwd));
    return 0;
}

/* Get CWD */
int vfs_get_cwd_path(char *buf, size_t size) {
    strcpy_safe(buf, cwd, size);
    return 0;
}
