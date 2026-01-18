/*
 * UnixOS Kernel - Ramfs (RAM Filesystem)
 * 
 * Simple in-memory filesystem for initial root filesystem.
 */

#include "fs/vfs.h"
#include "mm/kmalloc.h"
#include "printk.h"

/* ===================================================================== */
/* Ramfs structures */
/* ===================================================================== */

#define RAMFS_MAX_NAME      255
#define RAMFS_MAX_FILES     1024
#define RAMFS_BLOCK_SIZE    4096

struct ramfs_inode {
    ino_t ino;
    mode_t mode;
    uid_t uid;
    gid_t gid;
    size_t size;
    uint8_t *data;          /* File data */
    size_t data_capacity;   /* Allocated size */
    struct ramfs_inode *parent;
    struct ramfs_inode *children;   /* First child (for directories) */
    struct ramfs_inode *sibling;    /* Next sibling */
    char name[RAMFS_MAX_NAME + 1];
};

struct ramfs_sb_info {
    struct ramfs_inode *root;
    ino_t next_ino;
    size_t inode_count;
};

/* ===================================================================== */
/* Static data */
/* ===================================================================== */

static struct ramfs_sb_info ramfs_sb;

/* ===================================================================== */
/* Inode operations */
/* ===================================================================== */

static struct ramfs_inode *ramfs_alloc_inode(mode_t mode, const char *name)
{
    struct ramfs_inode *inode = kzalloc(sizeof(struct ramfs_inode), GFP_KERNEL);
    if (!inode) {
        return NULL;
    }
    
    inode->ino = ramfs_sb.next_ino++;
    inode->mode = mode;
    inode->uid = 0;
    inode->gid = 0;
    inode->size = 0;
    inode->data = NULL;
    inode->data_capacity = 0;
    inode->parent = NULL;
    inode->children = NULL;
    inode->sibling = NULL;
    
    /* Copy name */
    int i;
    for (i = 0; i < RAMFS_MAX_NAME && name[i]; i++) {
        inode->name[i] = name[i];
    }
    inode->name[i] = '\0';
    
    ramfs_sb.inode_count++;
    
    return inode;
}

static void ramfs_free_inode(struct ramfs_inode *inode)
{
    if (!inode) return;
    
    if (inode->data) {
        kfree(inode->data);
    }
    
    ramfs_sb.inode_count--;
    kfree(inode);
}

static struct ramfs_inode *ramfs_lookup_child(struct ramfs_inode *dir, const char *name)
{
    if (!S_ISDIR(dir->mode)) {
        return NULL;
    }
    
    struct ramfs_inode *child = dir->children;
    while (child) {
        /* Compare names */
        const char *a = child->name;
        const char *b = name;
        while (*a && *b && *a == *b) {
            a++;
            b++;
        }
        if (*a == '\0' && *b == '\0') {
            return child;
        }
        child = child->sibling;
    }
    
    return NULL;
}

static int ramfs_add_child(struct ramfs_inode *dir, struct ramfs_inode *child)
{
    if (!S_ISDIR(dir->mode)) {
        return -ENOTDIR;
    }
    
    child->parent = dir;
    child->sibling = dir->children;
    dir->children = child;
    
    return 0;
}

/* ===================================================================== */
/* File operations */
/* ===================================================================== */

static ssize_t ramfs_read(struct file *file, char *buf, size_t count, loff_t *pos)
{
    /* Get ramfs inode from file */
    struct ramfs_inode *inode = (struct ramfs_inode *)file->private_data;
    
    if (!inode || !inode->data) {
        return 0;
    }
    
    if (*pos >= (loff_t)inode->size) {
        return 0;
    }
    
    size_t available = inode->size - *pos;
    size_t to_read = count < available ? count : available;
    
    /* Copy data */
    for (size_t i = 0; i < to_read; i++) {
        buf[i] = inode->data[*pos + i];
    }
    
    *pos += to_read;
    return to_read;
}

static ssize_t ramfs_write(struct file *file, const char *buf, size_t count, loff_t *pos)
{
    struct ramfs_inode *inode = (struct ramfs_inode *)file->private_data;
    
    if (!inode) {
        return -EIO;
    }
    
    size_t new_size = *pos + count;
    
    /* Grow data buffer if needed */
    if (new_size > inode->data_capacity) {
        size_t new_cap = (new_size + RAMFS_BLOCK_SIZE - 1) & ~(RAMFS_BLOCK_SIZE - 1);
        uint8_t *new_data = kmalloc(new_cap, GFP_KERNEL);
        if (!new_data) {
            return -ENOMEM;
        }
        
        /* Copy old data */
        if (inode->data) {
            for (size_t i = 0; i < inode->size; i++) {
                new_data[i] = inode->data[i];
            }
            kfree(inode->data);
        }
        
        inode->data = new_data;
        inode->data_capacity = new_cap;
    }
    
    /* Write data */
    for (size_t i = 0; i < count; i++) {
        inode->data[*pos + i] = buf[i];
    }
    
    *pos += count;
    
    if (*pos > (loff_t)inode->size) {
        inode->size = *pos;
    }
    
    return count;
}

static int ramfs_open(struct inode *vfs_inode, struct file *file)
{
    /* Store ramfs inode in file private data */
    file->private_data = vfs_inode->i_private;
    return 0;
}

static int ramfs_release(struct inode *vfs_inode, struct file *file)
{
    (void)vfs_inode;
    file->private_data = NULL;
    return 0;
}

static const struct file_operations ramfs_file_ops = {
    .read = ramfs_read,
    .write = ramfs_write,
    .open = ramfs_open,
    .release = ramfs_release,
    .llseek = NULL,  /* Use default */
    .readdir = NULL,
    .ioctl = NULL,
    .mmap = NULL,
};

/* ===================================================================== */
/* Directory operations */
/* ===================================================================== */

static int ramfs_readdir_callback(void *ctx, const char *name, int len,
                                   loff_t offset, ino_t ino, unsigned type)
{
    (void)ctx;
    (void)offset;
    (void)ino;
    (void)type;
    
    /* Print entry for now */
    printk("%.*s\n", len, name);
    return 0;
}

static int ramfs_readdir(struct file *file, void *ctx,
                          int (*filldir)(void *, const char *, int, loff_t, ino_t, unsigned))
{
    struct ramfs_inode *dir = (struct ramfs_inode *)file->private_data;
    
    if (!dir || !S_ISDIR(dir->mode)) {
        return -ENOTDIR;
    }
    
    loff_t pos = 0;
    
    /* . and .. */
    if (filldir) {
        filldir(ctx, ".", 1, pos++, dir->ino, S_IFDIR >> 12);
        filldir(ctx, "..", 2, pos++, dir->parent ? dir->parent->ino : dir->ino, S_IFDIR >> 12);
    }
    
    /* Children */
    struct ramfs_inode *child = dir->children;
    while (child) {
        int len = 0;
        while (child->name[len]) len++;
        
        if (filldir) {
            filldir(ctx, child->name, len, pos++, child->ino, child->mode >> 12);
        }
        child = child->sibling;
    }
    
    return 0;
}

static const struct file_operations ramfs_dir_ops = {
    .read = NULL,
    .write = NULL,
    .open = ramfs_open,
    .release = ramfs_release,
    .llseek = NULL,
    .readdir = ramfs_readdir,
    .ioctl = NULL,
    .mmap = NULL,
};

static struct inode_operations ramfs_inode_ops; /* Forward decl */

static struct dentry *ramfs_lookup(struct inode *dir, struct dentry *dentry)
{
    struct ramfs_inode *ram_dir = (struct ramfs_inode *)dir->i_private;
    struct ramfs_inode *ram_child = ramfs_lookup_child(ram_dir, dentry->d_name);
    
    if (!ram_child) return NULL;
    
    /* Create VFS inode for child */
    struct inode *inode = kzalloc(sizeof(struct inode), GFP_KERNEL);
    if (!inode) return NULL;
    
    inode->i_ino = ram_child->ino;
    inode->i_mode = ram_child->mode;
    inode->i_size = ram_child->size;
    inode->i_sb = dir->i_sb;
    inode->i_op = &ramfs_inode_ops;
    inode->i_fop = S_ISDIR(inode->i_mode) ? &ramfs_dir_ops : &ramfs_file_ops;
    inode->i_private = ram_child;
    
    dentry->d_inode = inode;
    
    return NULL; /* NULL means success/found in cache (we just populated it) */
}

static int ramfs_create(struct inode *dir, struct dentry *dentry, mode_t mode)
{
    struct ramfs_inode *ram_dir = (struct ramfs_inode *)dir->i_private;
    struct ramfs_inode *ram_file = ramfs_alloc_inode(S_IFREG | mode, dentry->d_name);
    
    if (!ram_file) return -ENOMEM;
    
    ramfs_add_child(ram_dir, ram_file);
    
    /* Create VFS inode */
    struct inode *inode = kzalloc(sizeof(struct inode), GFP_KERNEL);
    if (!inode) return -ENOMEM;
    
    inode->i_ino = ram_file->ino;
    inode->i_mode = ram_file->mode;
    inode->i_size = 0;
    inode->i_sb = dir->i_sb;
    inode->i_op = &ramfs_inode_ops;
    inode->i_fop = &ramfs_file_ops;
    inode->i_private = ram_file;
    
    dentry->d_inode = inode;
    
    return 0;
}

static int ramfs_mkdir(struct inode *dir, struct dentry *dentry, mode_t mode)
{
    struct ramfs_inode *ram_dir = (struct ramfs_inode *)dir->i_private;
    struct ramfs_inode *ram_child = ramfs_alloc_inode(S_IFDIR | mode, dentry->d_name);
    
    if (!ram_child) return -ENOMEM;
    
    ramfs_add_child(ram_dir, ram_child);
    
    /* Create VFS inode */
    struct inode *inode = kzalloc(sizeof(struct inode), GFP_KERNEL);
    if (!inode) return -ENOMEM;
    
    inode->i_ino = ram_child->ino;
    inode->i_mode = ram_child->mode;
    inode->i_size = 0;
    inode->i_sb = dir->i_sb;
    inode->i_op = &ramfs_inode_ops;
    inode->i_fop = &ramfs_dir_ops;
    inode->i_private = ram_child;
    
    dentry->d_inode = inode;
    
    return 0;
}

static int ramfs_rename(struct inode *old_dir, struct dentry *old_dentry,
                        struct inode *new_dir, struct dentry *new_dentry)
{
    struct ramfs_inode *old_ram_dir = (struct ramfs_inode *)old_dir->i_private;
    struct ramfs_inode *new_ram_dir = (struct ramfs_inode *)new_dir->i_private;
    struct ramfs_inode *target = (struct ramfs_inode *)old_dentry->d_inode->i_private;
    
    if (!target) return -ENOENT;
    
    /* TODO: Check if new name exists (overwrite) */
    /* For now, just fail if exists for simplicity, or we should support overwrite? */
    /* Let's keep it simple: if new exists, fail */
    if (ramfs_lookup_child(new_ram_dir, new_dentry->d_name)) {
        return -EEXIST;
    }
    
    /* 1. Unlink from old_dir */
    if (old_ram_dir->children == target) {
        old_ram_dir->children = target->sibling;
    } else {
        struct ramfs_inode *curr = old_ram_dir->children;
        while (curr && curr->sibling != target) {
            curr = curr->sibling;
        }
        if (curr) curr->sibling = target->sibling;
    }
    
    /* 2. Link to new_dir */
    /* Only if different directory, but even if same dir we need to rename */
    if (old_ram_dir != new_ram_dir) {
        target->sibling = new_ram_dir->children;
        new_ram_dir->children = target;
        target->parent = new_ram_dir;
    } else {
        /* Re-link in same dir (it was removed above) */
        target->sibling = old_ram_dir->children;
        old_ram_dir->children = target;
    }
    
    /* 3. Rename */
    int i;
    for (i = 0; i < RAMFS_MAX_NAME && new_dentry->d_name[i]; i++) {
        target->name[i] = new_dentry->d_name[i];
    }
    target->name[i] = '\0';
    
    return 0;
}

static struct inode_operations ramfs_inode_ops = {
    .lookup = ramfs_lookup,
    .create = ramfs_create,
    .mkdir = ramfs_mkdir,
    .rmdir = NULL,
    .unlink = NULL,
    .rename = ramfs_rename,
};

/* ===================================================================== */
/* Mounting */
/* ===================================================================== */

static struct super_block *ramfs_mount(struct file_system_type *fs_type,
                                        int flags, const char *dev_name, void *data)
{
    (void)flags;
    (void)dev_name;
    (void)data;
    
    printk(KERN_INFO "RAMFS: Mounting ramfs\n");
    
    /* Initialize superblock info */
    ramfs_sb.next_ino = 1;
    ramfs_sb.inode_count = 0;
    
    /* Create ramfs root inode */
    ramfs_sb.root = ramfs_alloc_inode(S_IFDIR | 0755, "");
    if (!ramfs_sb.root) {
        return NULL;
    }
    
    /* Create superblock */
    static struct super_block sb;
    sb.s_blocksize = RAMFS_BLOCK_SIZE;
    sb.s_type = fs_type;
    sb.s_fs_info = &ramfs_sb;
    
    /* Create VFS root inode */
    static struct inode vfs_root_inode;
    vfs_root_inode.i_ino = ramfs_sb.root->ino;
    vfs_root_inode.i_mode = S_IFDIR | 0755;
    vfs_root_inode.i_size = 0;
    vfs_root_inode.i_sb = &sb;
    vfs_root_inode.i_op = &ramfs_inode_ops;
    vfs_root_inode.i_fop = &ramfs_dir_ops;
    vfs_root_inode.i_private = ramfs_sb.root; /* Link to ramfs inode */
    
    /* Create root dentry */
    static struct dentry root_dentry;
    root_dentry.d_name[0] = '/';
    root_dentry.d_name[1] = '\0';
    root_dentry.d_inode = &vfs_root_inode;  /* Point to VFS inode */
    root_dentry.d_parent = &root_dentry;
    root_dentry.d_child = NULL;
    root_dentry.d_sibling = NULL;
    root_dentry.d_sb = &sb;
    
    sb.s_root = &root_dentry;
    
    printk(KERN_INFO "RAMFS: Mounted successfully\n");
    
    return &sb;
}

static void ramfs_kill_sb(struct super_block *sb)
{
    (void)sb;
    printk(KERN_INFO "RAMFS: Unmounting\n");
    /* TODO: Free all inodes */
}

/* ===================================================================== */
/* Filesystem type */
/* ===================================================================== */

static struct file_system_type ramfs_fs_type = {
    .name = "ramfs",
    .fs_flags = 0,
    .mount = ramfs_mount,
    .kill_sb = ramfs_kill_sb,
    .next = NULL,
};

/* ===================================================================== */
/* Initialization */
/* ===================================================================== */

int ramfs_init(void)
{
    printk(KERN_INFO "RAMFS: Registering ramfs filesystem\n");
    return register_filesystem(&ramfs_fs_type);
}

/* ===================================================================== */
/* Helper: Create a file in ramfs */
/* ===================================================================== */

int ramfs_create_file(const char *path, mode_t mode, const char *content)
{
    if (!ramfs_sb.root) {
        return -ENOENT;
    }
    
    /* For now, just create in root */
    struct ramfs_inode *file = ramfs_alloc_inode(S_IFREG | mode, path);
    if (!file) {
        return -ENOMEM;
    }
    
    /* Copy content if provided */
    if (content) {
        size_t len = 0;
        while (content[len]) len++;
        
        file->data = kmalloc(len, GFP_KERNEL);
        if (file->data) {
            for (size_t i = 0; i < len; i++) {
                file->data[i] = content[i];
            }
            file->size = len;
            file->data_capacity = len;
        }
    }
    
    ramfs_add_child(ramfs_sb.root, file);
    
    printk(KERN_INFO "RAMFS: Created file '%s'\n", path);
    
    return 0;
}

/* Helper to find or create parent directory from path */
static struct ramfs_inode *ramfs_get_parent_dir(const char *path, char *filename)
{
    /* Find last '/' in path */
    const char *last_slash = NULL;
    for (const char *p = path; *p; p++) {
        if (*p == '/') last_slash = p;
    }
    
    if (!last_slash) {
        /* No directory, just filename - use root */
        int i = 0;
        while (path[i] && i < RAMFS_MAX_NAME) {
            filename[i] = path[i];
            i++;
        }
        filename[i] = '\0';
        return ramfs_sb.root;
    }
    
    /* Extract directory name */
    char dirname[RAMFS_MAX_NAME + 1];
    int len = last_slash - path;
    if (len > RAMFS_MAX_NAME) len = RAMFS_MAX_NAME;
    for (int i = 0; i < len; i++) {
        dirname[i] = path[i];
    }
    dirname[len] = '\0';
    
    /* Extract filename */
    int i = 0;
    const char *fname = last_slash + 1;
    while (fname[i] && i < RAMFS_MAX_NAME) {
        filename[i] = fname[i];
        i++;
    }
    filename[i] = '\0';
    
    /* Find directory */
    struct ramfs_inode *dir = ramfs_lookup_child(ramfs_sb.root, dirname);
    if (!dir) {
        return NULL;
    }
    
    return dir;
}

int ramfs_create_file_bytes(const char *path, mode_t mode, const uint8_t *data, size_t size)
{
    if (!ramfs_sb.root) {
        return -ENOENT;
    }

    char filename[RAMFS_MAX_NAME + 1];
    struct ramfs_inode *parent = ramfs_get_parent_dir(path, filename);
    if (!parent) {
        return -ENOENT;
    }

    struct ramfs_inode *file = ramfs_alloc_inode(S_IFREG | mode, filename);
    if (!file) {
        return -ENOMEM;
    }

    if (data && size > 0) {
        file->data = kmalloc(size, GFP_KERNEL);
        if (!file->data) {
            return -ENOMEM;
        }
        for (size_t i = 0; i < size; i++) {
            file->data[i] = data[i];
        }
        file->size = size;
        file->data_capacity = size;
    }

    ramfs_add_child(parent, file);
    printk(KERN_INFO "RAMFS: Created file '%s' (%lu bytes)\n", path, (unsigned long)size);
    return 0;
}

int ramfs_create_dir(const char *path, mode_t mode)
{
    if (!ramfs_sb.root) {
        return -ENOENT;
    }
    
    struct ramfs_inode *dir = ramfs_alloc_inode(S_IFDIR | mode, path);
    if (!dir) {
        return -ENOMEM;
    }
    
    ramfs_add_child(ramfs_sb.root, dir);
    
    printk(KERN_INFO "RAMFS: Created directory '%s'\n", path);
    
    return 0;
}
