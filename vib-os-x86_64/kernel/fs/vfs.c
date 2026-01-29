/*
 * VibCode x64 - Simple RAM-based Virtual Filesystem
 */

#include "../include/vfs.h"
#include "../include/kmalloc.h"
#include "../include/string.h"

/* Inode structure */
typedef struct inode {
  char name[MAX_NAME];
  char path[MAX_PATH];
  int type;           /* 0 = file, 1 = directory */
  uint8_t *data;
  size_t size;
  size_t capacity;
  int parent;         /* Parent inode index, -1 for root */
} inode_t;

/* Filesystem state */
static inode_t inodes[MAX_FILES];
static int inode_count = 0;
static file_t open_files[MAX_OPEN];
static int vfs_initialized = 0;

/* String helpers */
static int str_eq(const char *a, const char *b) {
  while (*a && *b) {
    if (*a != *b) return 0;
    a++; b++;
  }
  return *a == *b;
}

static void str_cpy(char *dst, const char *src, int max) {
  int i = 0;
  while (src[i] && i < max - 1) {
    dst[i] = src[i];
    i++;
  }
  dst[i] = '\0';
}

/* Find inode by path */
static int find_inode(const char *path) {
  for (int i = 0; i < inode_count; i++) {
    if (str_eq(inodes[i].path, path)) {
      return i;
    }
  }
  return -1;
}

/* Get parent path */
static void get_parent_path(const char *path, char *parent) {
  str_cpy(parent, path, MAX_PATH);
  int len = strlen(parent);
  
  /* Remove trailing slash */
  if (len > 1 && parent[len-1] == '/') {
    parent[--len] = '\0';
  }
  
  /* Find last slash */
  int last_slash = -1;
  for (int i = 0; i < len; i++) {
    if (parent[i] == '/') last_slash = i;
  }
  
  if (last_slash <= 0) {
    parent[0] = '/';
    parent[1] = '\0';
  } else {
    parent[last_slash] = '\0';
  }
}

/* Get filename from path */
static void get_filename(const char *path, char *name) {
  int len = strlen(path);
  int last_slash = -1;
  
  for (int i = 0; i < len; i++) {
    if (path[i] == '/') last_slash = i;
  }
  
  if (last_slash < 0) {
    str_cpy(name, path, MAX_NAME);
  } else {
    str_cpy(name, path + last_slash + 1, MAX_NAME);
  }
}

/* Create inode */
static int create_inode(const char *path, int type) {
  if (inode_count >= MAX_FILES) return -ENOSPC;
  if (find_inode(path) >= 0) return -EEXIST;
  
  int idx = inode_count++;
  inode_t *node = &inodes[idx];
  
  str_cpy(node->path, path, MAX_PATH);
  get_filename(path, node->name);
  node->type = type;
  node->data = NULL;
  node->size = 0;
  node->capacity = 0;
  
  /* Find parent */
  char parent_path[MAX_PATH];
  get_parent_path(path, parent_path);
  node->parent = find_inode(parent_path);
  
  return idx;
}

/* Initialize VFS */
void vfs_init(void) {
  if (vfs_initialized) return;
  
  kmalloc_init();
  
  /* Clear state */
  memset(inodes, 0, sizeof(inodes));
  memset(open_files, 0, sizeof(open_files));
  inode_count = 0;
  
  /* Create root directory */
  create_inode("/", 1);
  
  vfs_initialized = 1;
}

/* Seed initial filesystem content */
void vfs_seed_content(void) {
  /* Create directory structure */
  vfs_mkdir("/home");
  vfs_mkdir("/home/user");
  vfs_mkdir("/Desktop");
  vfs_mkdir("/Documents");
  vfs_mkdir("/Pictures");
  vfs_mkdir("/Downloads");
  vfs_mkdir("/bin");
  vfs_mkdir("/etc");
  
  /* Create some files */
  file_t *f;
  
  f = vfs_open("/Desktop/readme.txt", O_CREAT | O_WRONLY);
  if (f) {
    const char *txt = "Welcome to Vib-os!\n\nThis is a demo operating system.\n";
    vfs_write(f, txt, strlen(txt));
    vfs_close(f);
  }
  
  f = vfs_open("/Desktop/todo.txt", O_CREAT | O_WRONLY);
  if (f) {
    const char *txt = "TODO List:\n- Learn OS development\n- Build cool apps\n- Have fun!\n";
    vfs_write(f, txt, strlen(txt));
    vfs_close(f);
  }
  
  f = vfs_open("/etc/hostname", O_CREAT | O_WRONLY);
  if (f) {
    vfs_write(f, "vibcode", 7);
    vfs_close(f);
  }
  
  f = vfs_open("/etc/version", O_CREAT | O_WRONLY);
  if (f) {
    vfs_write(f, "1.0.0", 5);
    vfs_close(f);
  }
}

/* Open file */
file_t *vfs_open(const char *path, int flags) {
  if (!vfs_initialized) vfs_init();
  
  /* Find free file handle */
  int fd = -1;
  for (int i = 0; i < MAX_OPEN; i++) {
    if (!open_files[i].valid) {
      fd = i;
      break;
    }
  }
  if (fd < 0) return NULL;
  
  /* Find or create inode */
  int idx = find_inode(path);
  
  if (idx < 0) {
    if (flags & O_CREAT) {
      idx = create_inode(path, 0); /* Create file */
      if (idx < 0) return NULL;
    } else {
      return NULL;
    }
  }
  
  /* Don't open directories as files */
  if (inodes[idx].type == 1) return NULL;
  
  /* Truncate if requested */
  if (flags & O_TRUNC) {
    if (inodes[idx].data) {
      kfree(inodes[idx].data);
      inodes[idx].data = NULL;
    }
    inodes[idx].size = 0;
    inodes[idx].capacity = 0;
  }
  
  /* Setup file handle */
  file_t *f = &open_files[fd];
  f->valid = 1;
  f->inode = idx;
  f->flags = flags;
  f->pos = (flags & O_APPEND) ? inodes[idx].size : 0;
  
  return f;
}

/* Close file */
void vfs_close(file_t *file) {
  if (file && file->valid) {
    file->valid = 0;
  }
}

/* Read from file */
ssize_t vfs_read(file_t *file, void *buf, size_t count) {
  if (!file || !file->valid) return -EBADF;
  
  inode_t *node = &inodes[file->inode];
  if (node->type == 1) return -EISDIR;
  
  if (file->pos >= node->size) return 0;
  
  size_t available = node->size - file->pos;
  size_t to_read = (count < available) ? count : available;
  
  if (node->data) {
    memcpy(buf, node->data + file->pos, to_read);
  }
  
  file->pos += to_read;
  return to_read;
}

/* Write to file */
ssize_t vfs_write(file_t *file, const void *buf, size_t count) {
  if (!file || !file->valid) return -EBADF;
  
  inode_t *node = &inodes[file->inode];
  if (node->type == 1) return -EISDIR;
  
  size_t new_size = file->pos + count;
  
  /* Grow buffer if needed */
  if (new_size > node->capacity) {
    size_t new_cap = (new_size + 4095) & ~4095; /* Round up to 4KB */
    uint8_t *new_data = kmalloc(new_cap);
    if (!new_data) return -ENOMEM;
    
    if (node->data) {
      memcpy(new_data, node->data, node->size);
      kfree(node->data);
    }
    
    node->data = new_data;
    node->capacity = new_cap;
  }
  
  memcpy(node->data + file->pos, buf, count);
  file->pos += count;
  
  if (file->pos > node->size) {
    node->size = file->pos;
  }
  
  return count;
}

/* Seek in file */
int vfs_seek(file_t *file, int offset, int whence) {
  if (!file || !file->valid) return -EBADF;
  
  inode_t *node = &inodes[file->inode];
  size_t new_pos;
  
  switch (whence) {
    case SEEK_SET: new_pos = offset; break;
    case SEEK_CUR: new_pos = file->pos + offset; break;
    case SEEK_END: new_pos = node->size + offset; break;
    default: return -EINVAL;
  }
  
  file->pos = new_pos;
  return new_pos;
}

/* Create directory */
int vfs_mkdir(const char *path) {
  if (!vfs_initialized) vfs_init();
  return create_inode(path, 1);
}

/* Read directory */
int vfs_readdir(const char *path, dirent_t *entries, int max_entries) {
  if (!vfs_initialized) vfs_init();
  
  int dir_idx = find_inode(path);
  if (dir_idx < 0) return -ENOENT;
  if (inodes[dir_idx].type != 1) return -ENOTDIR;
  
  int count = 0;
  
  for (int i = 0; i < inode_count && count < max_entries; i++) {
    if (inodes[i].parent == dir_idx) {
      str_cpy(entries[count].name, inodes[i].name, MAX_NAME);
      entries[count].type = inodes[i].type;
      entries[count].size = inodes[i].size;
      count++;
    }
  }
  
  return count;
}

/* Get file info */
int vfs_stat(const char *path, size_t *size, int *is_dir) {
  int idx = find_inode(path);
  if (idx < 0) return -ENOENT;
  
  if (size) *size = inodes[idx].size;
  if (is_dir) *is_dir = inodes[idx].type;
  
  return 0;
}

/* Check if file exists */
int vfs_exists(const char *path) {
  return find_inode(path) >= 0;
}

/* Create file */
int vfs_create(const char *path) {
  if (!vfs_initialized) vfs_init();
  return create_inode(path, 0);
}

/* Delete file/directory */
int vfs_delete(const char *path) {
  int idx = find_inode(path);
  if (idx < 0) return -ENOENT;
  if (idx == 0) return -EINVAL; /* Can't delete root */
  
  /* Check if directory is empty */
  if (inodes[idx].type == 1) {
    for (int i = 0; i < inode_count; i++) {
      if (inodes[i].parent == idx) {
        return -ENOTEMPTY;
      }
    }
  }
  
  /* Free data */
  if (inodes[idx].data) {
    kfree(inodes[idx].data);
  }
  
  /* Remove by swapping with last */
  if (idx < inode_count - 1) {
    inodes[idx] = inodes[inode_count - 1];
    /* Update children's parent pointers */
    for (int i = 0; i < inode_count - 1; i++) {
      if (inodes[i].parent == inode_count - 1) {
        inodes[i].parent = idx;
      }
    }
  }
  inode_count--;
  
  return 0;
}

/* Rename file/directory */
int vfs_rename(const char *oldpath, const char *newpath) {
  int idx = find_inode(oldpath);
  if (idx < 0) return -ENOENT;
  if (find_inode(newpath) >= 0) return -EEXIST;
  
  str_cpy(inodes[idx].path, newpath, MAX_PATH);
  get_filename(newpath, inodes[idx].name);
  
  char parent_path[MAX_PATH];
  get_parent_path(newpath, parent_path);
  inodes[idx].parent = find_inode(parent_path);
  
  return 0;
}
