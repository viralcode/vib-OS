/*
 * UnixOS Kernel - Virtual Filesystem Implementation
 */

#include "fs/vfs.h"
#include "printk.h"

/* ===================================================================== */
/* Static data */
/* ===================================================================== */

/* Registered filesystems */
static struct file_system_type *file_systems = NULL;

/* Mount points */
static struct vfsmount *mounts[MAX_MOUNTS];
static int mount_count = 0;

/* Root filesystem */
static struct vfsmount *root_mount = NULL;
static struct dentry *root_dentry = NULL;

/* ===================================================================== */
/* Helper functions */
/* ===================================================================== */

static struct file_system_type *find_filesystem(const char *name) {
  struct file_system_type *fs = file_systems;
  while (fs) {
    /* Compare names */
    const char *a = fs->name;
    const char *b = name;
    while (*a && *b && *a == *b) {
      a++;
      b++;
    }
    if (*a == '\0' && *b == '\0') {
      return fs;
    }
    fs = fs->next;
  }
  return NULL;
}

static int path_compare(const char *a, const char *b) {
  while (*a && *b && *a == *b) {
    a++;
    b++;
  }
  return *a - *b;
}

/* ===================================================================== */
/* VFS initialization */
/* ===================================================================== */

void vfs_init(void) {
  printk(KERN_INFO "VFS: Initializing virtual filesystem\n");

  /* Clear mount table */
  for (int i = 0; i < MAX_MOUNTS; i++) {
    mounts[i] = NULL;
  }

  /* TODO: Register built-in filesystems */
  /* register_filesystem(&ramfs_type); */
  /* register_filesystem(&procfs_type); */
  /* register_filesystem(&sysfs_type); */
  /* register_filesystem(&devfs_type); */

  printk(KERN_INFO "VFS: Initialized\n");
}

/* ===================================================================== */
/* Filesystem registration */
/* ===================================================================== */

int register_filesystem(struct file_system_type *fs) {
  if (!fs || !fs->name) {
    return -EINVAL;
  }

  /* Check for duplicate */
  if (find_filesystem(fs->name)) {
    printk(KERN_WARNING "VFS: Filesystem '%s' already registered\n", fs->name);
    return -EBUSY;
  }

  /* Add to list */
  fs->next = file_systems;
  file_systems = fs;

  printk(KERN_INFO "VFS: Registered filesystem '%s'\n", fs->name);

  return 0;
}

/* ===================================================================== */
/* File operations */
/* ===================================================================== */

#include "mm/kmalloc.h"

/* ===================================================================== */
/* Path lookup */
/* ===================================================================== */

static struct dentry *vfs_lookup_path(const char *path, const char **filename) {
  if (!root_dentry)
    return NULL;

  struct dentry *curr = root_dentry;
  char *p = (char *)path;

  /* Skip leading / */
  while (*p == '/')
    p++;

  if (*p == '\0') {
    if (filename)
      *filename = NULL;
    return curr;
  }

  static char buf[NAME_MAX + 1];

  while (*p) {
    /* Extract next component */
    int len = 0;
    char *start = p;
    while (*p && *p != '/') {
      if (len < NAME_MAX)
        buf[len++] = *p;
      p++;
    }
    buf[len] = '\0';

    while (*p == '/')
      p++;

    /* If this is the last component, return parent and filename */
    if (*p == '\0' && filename) {
      *filename = start; /* Pointer into original string - careful */
      /* Actually, we need to copy it because original might be const */
      /* But caller usually passes non-const or we can just return curr */
      /* Better design: return parent dentry and pointer to last component in
       * path */
      return curr; /* curr is the directory containing the file */
                   /* Wait, this logic is tricky. Let's do simple traversal */
    }

    /* Lookup child */
    if (!curr->d_inode || !curr->d_inode->i_op ||
        !curr->d_inode->i_op->lookup) {
      return NULL;
    }

    struct dentry target;
    for (int i = 0; i <= len; i++)
      target.d_name[i] = buf[i];

    /* In this simplified VFS, lookup populates the dentry if found */
    /* We need to allocate a real dentry to return/store */
    /* For now, simplified: rely on ramfs creating the inode and we assume we
     * traverse */

    /* Simple hack for ramfs traversal without full dcache: */
    /* We construct a dummy dentry, pass to lookup. If lookup populates d_inode,
     * we proceed. */

    /* Allocate a dentry to be safe/consistent */
    struct dentry *child = kzalloc(sizeof(struct dentry), GFP_KERNEL);
    if (!child)
      return NULL;

    for (int i = 0; i <= len; i++)
      child->d_name[i] = buf[i];
    child->d_parent = curr;
    child->d_sb = curr->d_sb;

    if (curr->d_inode->i_op->lookup(curr->d_inode, child) != NULL) {
      /* If it returns a dentry, use it */
      /* (Not implemented in ramfs, it returns NULL on success with populated
       * pointer) */
    }

    if (!child->d_inode) {
      /* Not found */
      kfree(child);
      return NULL;
    }

    curr = child;
  }

  if (filename)
    *filename = NULL;
  return curr;
}

/* Helper to find parent and last component */
static struct dentry *vfs_lookup_parent(const char *path, char *name_buf) {
  if (!root_dentry)
    return NULL;

  struct dentry *curr = root_dentry;
  char *p = (char *)path;

  /* Skip leading / */
  while (*p == '/')
    p++;

  if (*p == '\0')
    return NULL; /* Root has no parent */

  static char buf[NAME_MAX + 1];

  while (*p) {
    /* Extract next component */
    int len = 0;
    while (*p && *p != '/') {
      if (len < NAME_MAX)
        buf[len++] = *p;
      p++;
    }
    buf[len] = '\0';

    while (*p == '/')
      p++;

    if (*p == '\0') {
      /* This was the last component */
      if (name_buf) {
        for (int i = 0; i <= len; i++)
          name_buf[i] = buf[i];
      }
      return curr;
    }

    /* Traverse down */
    if (!curr->d_inode || !curr->d_inode->i_op ||
        !curr->d_inode->i_op->lookup) {
      return NULL;
    }

    struct dentry *child = kzalloc(sizeof(struct dentry), GFP_KERNEL);
    if (!child)
      return NULL;

    for (int i = 0; i <= len; i++)
      child->d_name[i] = buf[i];

    /* Assume success for now (lookup populates child->d_inode) */
    curr->d_inode->i_op->lookup(curr->d_inode, child);

    if (!child->d_inode) {
      kfree(child);
      return NULL;
    }
    curr = child;
  }

  return NULL;
}

/* Redefine vfs_open with lookup */
struct file *vfs_open(const char *path, int flags, mode_t mode) {
  /* Special case for root */
  if (path[0] == '/' && path[1] == '\0') {
    struct file *f = kzalloc(sizeof(struct file), GFP_KERNEL);
    f->f_dentry = root_dentry;
    f->f_op = root_dentry->d_inode->i_fop;
    f->private_data = root_dentry->d_inode->i_private;
    f->f_mode = mode;
    f->f_flags = flags;
    f->f_count.counter = 1;
    return f;
  }

  char name[NAME_MAX + 1];
  struct dentry *parent = vfs_lookup_parent(path, name);

  if (!parent) {
    /* Try full lookup (might be exact match on an intermediate node? Unlikely
     * for open) */
    /* Or file exists in root */
    if (root_dentry)
      parent = root_dentry;

    /* Extract name from /name */
    const char *p = path;
    while (*p == '/')
      p++;
    int i = 0;
    while (*p && *p != '/') {
      if (i < NAME_MAX)
        name[i++] = *p;
      p++;
    }
    name[i] = '\0';
    if (*p != '\0')
      return NULL; /* Path had more components but parent lookup failed */
  }

  /* Now look for the file in parent */
  struct dentry *child = kzalloc(sizeof(struct dentry), GFP_KERNEL);
  for (int i = 0; i < NAME_MAX && name[i]; i++)
    child->d_name[i] = name[i];

  if (parent->d_inode && parent->d_inode->i_op &&
      parent->d_inode->i_op->lookup) {
    parent->d_inode->i_op->lookup(parent->d_inode, child);
  }

  if (!child->d_inode) {
    /* Check O_CREAT */
    if (flags & O_CREAT) {
      /* Create it */
      if (parent->d_inode->i_op->create) {
        int ret = parent->d_inode->i_op->create(parent->d_inode, child, mode);
        if (ret != 0) {
          kfree(child);
          return NULL;
        }
      }
    } else {
      kfree(child);
      return NULL;
    }
  }

  struct file *f = kzalloc(sizeof(struct file), GFP_KERNEL);
  if (!f)
    return NULL;

  f->f_dentry = child;
  f->f_op = child->d_inode->i_fop;
  f->private_data = child->d_inode->i_private;
  f->f_mode = mode;
  f->f_flags = flags;
  f->f_count.counter = 1;

  if (f->f_op && f->f_op->open) {
    f->f_op->open(child->d_inode, f);
  }

  return f;
}

int vfs_create(const char *path, mode_t mode) {
  char name[NAME_MAX + 1];
  struct dentry *parent = vfs_lookup_parent(path, name);
  if (!parent)
    return -ENOENT;

  struct dentry *child = kzalloc(sizeof(struct dentry), GFP_KERNEL);
  for (int i = 0; i < NAME_MAX && name[i]; i++)
    child->d_name[i] = name[i];

  if (!parent->d_inode->i_op || !parent->d_inode->i_op->create)
    return -EPERM;

  return parent->d_inode->i_op->create(parent->d_inode, child, mode);
}

int vfs_mkdir(const char *path, mode_t mode) {
  char name[NAME_MAX + 1];
  struct dentry *parent = vfs_lookup_parent(path, name);
  if (!parent)
    return -ENOENT;

  struct dentry *child = kzalloc(sizeof(struct dentry), GFP_KERNEL);
  for (int i = 0; i < NAME_MAX && name[i]; i++)
    child->d_name[i] = name[i];

  if (!parent->d_inode->i_op || !parent->d_inode->i_op->mkdir)
    return -EPERM;

  return parent->d_inode->i_op->mkdir(parent->d_inode, child, mode);
}

int vfs_readdir(struct file *file, void *ctx,
                int (*filldir)(void *, const char *, int, loff_t, ino_t,
                               unsigned)) {
  if (!file || !file->f_op || !file->f_op->readdir) {
    return -EINVAL;
  }
  return file->f_op->readdir(file, ctx, filldir);
}

int vfs_close(struct file *file) {
  if (!file)
    return -EBADF;
  if (file->f_op && file->f_op->release && file->f_dentry) {
    file->f_op->release(file->f_dentry->d_inode, file);
  }
  file->f_count.counter--;
  if (file->f_count.counter <= 0) {
    kfree(file);
  }
  return 0;
}

ssize_t vfs_read(struct file *file, char *buf, size_t count) {
  if (!file)
    return -EBADF;
  if (!buf)
    return -EFAULT;
  if (!file->f_op || !file->f_op->read)
    return -EINVAL;
  return file->f_op->read(file, buf, count, &file->f_pos);
}

ssize_t vfs_write(struct file *file, const char *buf, size_t count) {
  if (!file)
    return -EBADF;
  if (!buf)
    return -EFAULT;
  if (!file->f_op || !file->f_op->write)
    return -EINVAL;
  return file->f_op->write(file, buf, count, &file->f_pos);
}

loff_t vfs_lseek(struct file *file, loff_t offset, int whence) {
  if (!file)
    return -EBADF;
  loff_t new_pos;
  struct inode *inode = file->f_dentry ? file->f_dentry->d_inode : NULL;

  switch (whence) {
  case SEEK_SET:
    new_pos = offset;
    break;
  case SEEK_CUR:
    new_pos = file->f_pos + offset;
    break;
  case SEEK_END:
    if (!inode)
      return -EINVAL;
    new_pos = inode->i_size + offset;
    break;
  default:
    return -EINVAL;
  }
  if (new_pos < 0)
    return -EINVAL;
  file->f_pos = new_pos;
  return new_pos;
}

int vfs_rmdir(const char *path) {
  char name[NAME_MAX + 1];
  struct dentry *parent = vfs_lookup_parent(path, name);
  if (!parent)
    return -ENOENT;

  struct dentry *child = kzalloc(sizeof(struct dentry), GFP_KERNEL);
  if (!child)
    return -ENOMEM;

  int i;
  for (i = 0; i < NAME_MAX && name[i]; i++)
    child->d_name[i] = name[i];
  child->d_name[i] = '\0';

  /* Lookup the target */
  if (parent->d_inode->i_op && parent->d_inode->i_op->lookup) {
    parent->d_inode->i_op->lookup(parent->d_inode, child);
  }

  if (!child->d_inode) {
    kfree(child);
    return -ENOENT;
  }

  /* Must be a directory */
  if (!S_ISDIR(child->d_inode->i_mode)) {
    kfree(child);
    return -ENOTDIR;
  }

  /* Check if rmdir operation is supported */
  if (!parent->d_inode->i_op || !parent->d_inode->i_op->rmdir) {
    kfree(child);
    return -EPERM;
  }

  int ret = parent->d_inode->i_op->rmdir(parent->d_inode, child);
  kfree(child);
  return ret;
}

int vfs_unlink(const char *path) {
  char name[NAME_MAX + 1];
  struct dentry *parent = vfs_lookup_parent(path, name);
  if (!parent)
    return -ENOENT;

  struct dentry *child = kzalloc(sizeof(struct dentry), GFP_KERNEL);
  if (!child)
    return -ENOMEM;

  int i;
  for (i = 0; i < NAME_MAX && name[i]; i++)
    child->d_name[i] = name[i];
  child->d_name[i] = '\0';

  /* Lookup the target */
  if (parent->d_inode->i_op && parent->d_inode->i_op->lookup) {
    parent->d_inode->i_op->lookup(parent->d_inode, child);
  }

  if (!child->d_inode) {
    kfree(child);
    return -ENOENT;
  }

  /* Must not be a directory (use rmdir for that) */
  if (S_ISDIR(child->d_inode->i_mode)) {
    kfree(child);
    return -EISDIR;
  }

  /* Check if unlink operation is supported */
  if (!parent->d_inode->i_op || !parent->d_inode->i_op->unlink) {
    kfree(child);
    return -EPERM;
  }

  int ret = parent->d_inode->i_op->unlink(parent->d_inode, child);
  kfree(child);
  return ret;
}
int vfs_rename(const char *old, const char *new) {
  char old_name_buf[NAME_MAX + 1];
  struct dentry *old_parent = vfs_lookup_parent(old, old_name_buf);
  if (!old_parent)
    return -ENOENT;

  char new_name_buf[NAME_MAX + 1];
  struct dentry *new_parent = vfs_lookup_parent(new, new_name_buf);
  if (!new_parent)
    return -ENOENT;

  /* Lookup full old dentry */
  struct dentry *old_child = kzalloc(sizeof(struct dentry), GFP_KERNEL);
  int i;
  for (i = 0; i < NAME_MAX && old_name_buf[i]; i++)
    old_child->d_name[i] = old_name_buf[i];
  old_child->d_name[i] = '\0';

  if (old_parent->d_inode->i_op && old_parent->d_inode->i_op->lookup) {
    old_parent->d_inode->i_op->lookup(old_parent->d_inode, old_child);
  }

  if (!old_child->d_inode) {
    kfree(old_child);
    return -ENOENT;
  }

  /* Construct new dentry pattern */
  struct dentry *new_child = kzalloc(sizeof(struct dentry), GFP_KERNEL);
  for (i = 0; i < NAME_MAX && new_name_buf[i]; i++)
    new_child->d_name[i] = new_name_buf[i];
  new_child->d_name[i] = '\0';

  /* Check if operation supported */
  if (!old_parent->d_inode->i_op || !old_parent->d_inode->i_op->rename) {
    kfree(old_child);
    kfree(new_child);
    return -EPERM; /* Should be ENOSYS/EPERM */
  }

  int ret = old_parent->d_inode->i_op->rename(old_parent->d_inode, old_child,
                                              new_parent->d_inode, new_child);

  kfree(old_child);
  kfree(new_child);
  return ret;
}

/* ===================================================================== */
/* Mount operations */
/* ===================================================================== */

int vfs_mount(const char *source, const char *target, const char *fstype,
              unsigned long flags, const void *data) {
  (void)flags;
  (void)data;

  printk(KERN_INFO "VFS: mount('%s', '%s', '%s')\n", source, target, fstype);

  /* Find filesystem type */
  struct file_system_type *fs = find_filesystem(fstype);
  if (!fs) {
    printk(KERN_ERR "VFS: Unknown filesystem type '%s'\n", fstype);
    return -ENODEV;
  }

  /* Check mount limit */
  if (mount_count >= MAX_MOUNTS) {
    return -ENOMEM;
  }

  /* Call filesystem's mount function */
  if (!fs->mount) {
    return -ENOSYS;
  }

  struct super_block *sb = fs->mount(fs, flags, source, (void *)data);
  if (!sb) {
    return -EIO;
  }

  /* Create mount structure */
  /* TODO: Allocate properly */
  static struct vfsmount mount_pool[MAX_MOUNTS];
  struct vfsmount *mnt = &mount_pool[mount_count];

  mnt->mnt_root = sb->s_root;
  mnt->mnt_sb = sb;
  mnt->mnt_mountpoint = NULL; /* TODO: Find target dentry */
  mnt->mnt_parent = root_mount;

  /* Copy device name */
  int i;
  for (i = 0; i < 63 && source[i]; i++) {
    mnt->mnt_devname[i] = source[i];
  }
  mnt->mnt_devname[i] = '\0';

  mounts[mount_count++] = mnt;

  /* If mounting root, set root_mount */
  if (path_compare(target, "/") == 0) {
    root_mount = mnt;
    root_dentry = sb->s_root;
  }

  printk(KERN_INFO "VFS: Mounted '%s' on '%s'\n", source, target);

  return 0;
}

int vfs_umount(const char *target) {
  printk(KERN_INFO "VFS: umount('%s')\n", target);

  /* Find mount point */
  for (int i = 0; i < mount_count; i++) {
    if (mounts[i] && mounts[i]->mnt_root) {
      /* TODO: Compare mount point */
      /* For now, just mark as unmounted */
    }
  }

  return -ENOSYS;
}
