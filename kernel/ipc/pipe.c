/*
 * UnixOS Kernel - Pipe Implementation
 */

#include "fs/vfs.h"
#include "mm/kmalloc.h"
#include "printk.h"

/* ===================================================================== */
/* Pipe structure */
/* ===================================================================== */

#define PIPE_SIZE 4096 /* Pipe buffer size */

struct pipe {
  uint8_t *buffer;   /* Ring buffer */
  size_t read_pos;   /* Read position */
  size_t write_pos;  /* Write position */
  size_t count;      /* Bytes in buffer */
  int readers;       /* Number of readers */
  int writers;       /* Number of writers */
  volatile int lock; /* Simple spinlock */
};

/* ===================================================================== */
/* Pipe operations */
/* ===================================================================== */

static void pipe_lock(struct pipe *p) {
  while (__atomic_test_and_set(&p->lock, __ATOMIC_ACQUIRE)) {
#ifdef ARCH_ARM64
    asm volatile("yield");
#elif defined(ARCH_X86_64) || defined(ARCH_X86)
    asm volatile("pause");
#endif
  }
}

static void pipe_unlock(struct pipe *p) {
  __atomic_clear(&p->lock, __ATOMIC_RELEASE);
}

static ssize_t pipe_read(struct file *file, char *buf, size_t count,
                         loff_t *pos) {
  (void)pos;

  struct pipe *p = (struct pipe *)file->private_data;
  if (!p) {
    return -EIO;
  }

  pipe_lock(p);

  /* No writers and empty buffer = EOF */
  if (p->writers == 0 && p->count == 0) {
    pipe_unlock(p);
    return 0;
  }

  /* Wait for data (busy wait for now) */
  while (p->count == 0 && p->writers > 0) {
    pipe_unlock(p);
#ifdef ARCH_ARM64
    asm volatile("yield");
#elif defined(ARCH_X86_64) || defined(ARCH_X86)
    asm volatile("pause");
#endif
    pipe_lock(p);
  }

  size_t to_read = count < p->count ? count : p->count;

  for (size_t i = 0; i < to_read; i++) {
    buf[i] = p->buffer[p->read_pos];
    p->read_pos = (p->read_pos + 1) % PIPE_SIZE;
  }

  p->count -= to_read;

  pipe_unlock(p);

  return to_read;
}

static ssize_t pipe_write(struct file *file, const char *buf, size_t count,
                          loff_t *pos) {
  (void)pos;

  struct pipe *p = (struct pipe *)file->private_data;
  if (!p) {
    return -EIO;
  }

  pipe_lock(p);

  /* No readers = SIGPIPE (for now, just return error) */
  if (p->readers == 0) {
    pipe_unlock(p);
    return -EPIPE;
  }

  size_t written = 0;

  while (written < count) {
    /* Wait for space */
    while (p->count >= PIPE_SIZE && p->readers > 0) {
      pipe_unlock(p);
#ifdef ARCH_ARM64
      asm volatile("yield");
#elif defined(ARCH_X86_64) || defined(ARCH_X86)
      asm volatile("pause");
#endif
      pipe_lock(p);
    }

    if (p->readers == 0) {
      pipe_unlock(p);
      return written > 0 ? written : -EPIPE;
    }

    size_t space = PIPE_SIZE - p->count;
    size_t to_write = (count - written) < space ? (count - written) : space;

    for (size_t i = 0; i < to_write; i++) {
      p->buffer[p->write_pos] = buf[written + i];
      p->write_pos = (p->write_pos + 1) % PIPE_SIZE;
    }

    p->count += to_write;
    written += to_write;
  }

  pipe_unlock(p);

  return written;
}

static int pipe_release_read(struct inode *inode, struct file *file) {
  (void)inode;

  struct pipe *p = (struct pipe *)file->private_data;
  if (p) {
    pipe_lock(p);
    p->readers--;

    /* Free pipe if no users */
    if (p->readers == 0 && p->writers == 0) {
      pipe_unlock(p);
      kfree(p->buffer);
      kfree(p);
    } else {
      pipe_unlock(p);
    }
  }

  return 0;
}

static int pipe_release_write(struct inode *inode, struct file *file) {
  (void)inode;

  struct pipe *p = (struct pipe *)file->private_data;
  if (p) {
    pipe_lock(p);
    p->writers--;

    if (p->readers == 0 && p->writers == 0) {
      pipe_unlock(p);
      kfree(p->buffer);
      kfree(p);
    } else {
      pipe_unlock(p);
    }
  }

  return 0;
}

static const struct file_operations pipe_read_ops = {
    .read = pipe_read,
    .write = NULL,
    .release = pipe_release_read,
};

static const struct file_operations pipe_write_ops = {
    .read = NULL,
    .write = pipe_write,
    .release = pipe_release_write,
};

/* ===================================================================== */
/* Pipe creation */
/* ===================================================================== */

int do_pipe(struct file **read_file, struct file **write_file) {
  /* Allocate pipe structure */
  struct pipe *p = kzalloc(sizeof(struct pipe), GFP_KERNEL);
  if (!p) {
    return -ENOMEM;
  }

  p->buffer = kmalloc(PIPE_SIZE, GFP_KERNEL);
  if (!p->buffer) {
    kfree(p);
    return -ENOMEM;
  }

  p->read_pos = 0;
  p->write_pos = 0;
  p->count = 0;
  p->readers = 1;
  p->writers = 1;
  p->lock = 0;

  /* Allocate file structures */
  struct file *rf = kzalloc(sizeof(struct file), GFP_KERNEL);
  struct file *wf = kzalloc(sizeof(struct file), GFP_KERNEL);

  if (!rf || !wf) {
    kfree(rf);
    kfree(wf);
    kfree(p->buffer);
    kfree(p);
    return -ENOMEM;
  }

  rf->f_op = &pipe_read_ops;
  rf->f_flags = O_RDONLY;
  rf->private_data = p;
  rf->f_count.counter = 1;

  wf->f_op = &pipe_write_ops;
  wf->f_flags = O_WRONLY;
  wf->private_data = p;
  wf->f_count.counter = 1;

  *read_file = rf;
  *write_file = wf;

  return 0;
}
