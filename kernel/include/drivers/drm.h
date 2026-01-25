/*
 * Vib-OS - Direct Rendering Manager (DRM) Core
 *
 * DRM-like abstraction layer for GPU drivers providing:
 * - Device management
 * - GEM buffer object management
 * - Command submission framework
 * - Fence/sync primitives
 */

#ifndef DRIVERS_DRM_H
#define DRIVERS_DRM_H

#include "drivers/pci.h"
#include "types.h"

/* ===================================================================== */
/* Forward Declarations */
/* ===================================================================== */

struct drm_device;
struct drm_gem_object;
struct drm_fence;
struct drm_file;

/* ===================================================================== */
/* DRM Device Types */
/* ===================================================================== */

typedef enum {
  DRM_DEVICE_VIRTIO = 0,
  DRM_DEVICE_INTEL,
  DRM_DEVICE_AMD,
  DRM_DEVICE_NVIDIA,
  DRM_DEVICE_SOFTWARE,
} drm_device_type_t;

/* ===================================================================== */
/* GEM (Graphics Execution Manager) Buffer Objects */
/* ===================================================================== */

#define GEM_FLAG_MAPPABLE 0x0001
#define GEM_FLAG_CONTIGUOUS 0x0002
#define GEM_FLAG_SCANOUT 0x0004
#define GEM_FLAG_RENDER 0x0008
#define GEM_FLAG_WRITE 0x0010
#define GEM_FLAG_READ 0x0020
#define GEM_FLAG_CACHED 0x0040
#define GEM_FLAG_WC 0x0080 /* Write-combined */

typedef struct drm_gem_object {
  /* Identity */
  uint32_t handle; /* User-visible handle */
  uint64_t size;   /* Size in bytes */
  uint32_t flags;

  /* Reference counting */
  int refcount;

  /* Physical backing */
  phys_addr_t *pages; /* Array of page physical addresses */
  uint32_t page_count;

  /* GPU address (if bound) */
  uint64_t gpu_addr;
  bool bound;

  /* CPU mapping */
  void *vaddr; /* Kernel virtual address (if mapped) */
  bool mapped;

  /* Ownership */
  struct drm_device *dev;
  struct drm_file *file;

  /* Linked list */
  struct drm_gem_object *next;
} drm_gem_object_t;

/* ===================================================================== */
/* Fence/Sync Objects */
/* ===================================================================== */

typedef enum {
  FENCE_STATE_ACTIVE = 0,
  FENCE_STATE_SIGNALED,
  FENCE_STATE_ERROR,
} fence_state_t;

typedef struct drm_fence {
  uint64_t seqno; /* Sequence number */
  fence_state_t state;

  /* Signaling */
  uint64_t signal_time; /* Timestamp when signaled */

  /* Ownership */
  struct drm_device *dev;

  /* Linked list */
  struct drm_fence *next;
} drm_fence_t;

/* ===================================================================== */
/* Command Submission */
/* ===================================================================== */

#define CMD_FLAG_FENCE 0x01       /* Insert fence after command */
#define CMD_FLAG_WAIT 0x02        /* Wait for completion */
#define CMD_FLAG_NO_IMPLICIT 0x04 /* No implicit sync */

typedef struct drm_command {
  void *cmd_data;    /* Command buffer data */
  uint32_t cmd_size; /* Size in bytes */
  uint32_t flags;

  /* Buffers referenced by this command */
  drm_gem_object_t **bo_list;
  uint32_t bo_count;

  /* Fence for completion tracking */
  drm_fence_t *fence;
} drm_command_t;

/* ===================================================================== */
/* Ring Buffer */
/* ===================================================================== */

typedef struct drm_ring {
  /* Ring memory */
  phys_addr_t ring_phys; /* Physical address of ring */
  void *ring_vaddr;      /* Virtual address of ring */
  uint32_t ring_size;    /* Size in bytes */

  /* Ring state */
  uint32_t head;  /* Consumer position */
  uint32_t tail;  /* Producer position */
  uint32_t space; /* Available space */

  /* Fence tracking */
  uint64_t last_submitted;
  uint64_t last_completed;

  /* Ownership */
  struct drm_device *dev;
  int ring_id;
} drm_ring_t;

/* ===================================================================== */
/* Display Mode Setting (KMS-like) */
/* ===================================================================== */

typedef struct drm_mode {
  uint32_t width;
  uint32_t height;
  uint32_t refresh;
  uint32_t flags;
} drm_mode_t;

typedef struct drm_framebuffer {
  uint32_t fb_id;
  uint32_t width;
  uint32_t height;
  uint32_t pitch;
  uint32_t format; /* DRM_FORMAT_* */

  drm_gem_object_t *bo; /* Backing buffer object */

  struct drm_device *dev;
  struct drm_framebuffer *next;
} drm_framebuffer_t;

typedef struct drm_crtc {
  uint32_t crtc_id;
  bool enabled;

  drm_mode_t mode;
  drm_framebuffer_t *fb; /* Current framebuffer */

  /* Cursor */
  int32_t cursor_x;
  int32_t cursor_y;
  drm_gem_object_t *cursor_bo;

  struct drm_device *dev;
} drm_crtc_t;

/* ===================================================================== */
/* DRM Device Structure */
/* ===================================================================== */

/* Driver operations */
typedef struct drm_driver_ops {
  const char *name;
  drm_device_type_t type;

  /* Device lifecycle */
  int (*init)(struct drm_device *dev);
  void (*fini)(struct drm_device *dev);

  /* GEM operations */
  drm_gem_object_t *(*gem_create)(struct drm_device *dev, uint64_t size,
                                  uint32_t flags);
  void (*gem_free)(struct drm_device *dev, drm_gem_object_t *bo);
  int (*gem_mmap)(struct drm_device *dev, drm_gem_object_t *bo);
  void (*gem_munmap)(struct drm_device *dev, drm_gem_object_t *bo);

  /* Command submission */
  int (*submit)(struct drm_device *dev, drm_command_t *cmd);

  /* Fence operations */
  drm_fence_t *(*fence_create)(struct drm_device *dev);
  int (*fence_wait)(struct drm_device *dev, drm_fence_t *fence,
                    uint64_t timeout_ns);
  bool (*fence_signaled)(struct drm_device *dev, drm_fence_t *fence);

  /* Display operations */
  int (*set_mode)(struct drm_device *dev, drm_mode_t *mode);
  int (*set_scanout)(struct drm_device *dev, drm_framebuffer_t *fb);
  int (*page_flip)(struct drm_device *dev, drm_framebuffer_t *fb);

  /* 3D acceleration */
  int (*create_context)(struct drm_device *dev, struct drm_file *file);
  void (*destroy_context)(struct drm_device *dev, struct drm_file *file);
} drm_driver_ops_t;

typedef struct drm_device {
  /* Identity */
  int dev_id;
  const char *name;
  drm_device_type_t type;

  /* PCI device */
  pci_device_t *pci;

  /* Driver operations */
  const drm_driver_ops_t *ops;
  void *driver_data;

  /* MMIO regions */
  void *mmio;
  uint64_t mmio_size;

  /* Memory management */
  uint64_t vram_size;
  uint64_t gart_size;

  /* GEM objects */
  drm_gem_object_t *gem_list;
  uint32_t gem_handle_counter;

  /* Fences */
  drm_fence_t *fence_list;
  uint64_t fence_seqno;

  /* Rings */
  drm_ring_t *rings[4]; /* Up to 4 command rings */
  int ring_count;

  /* Display */
  drm_crtc_t crtc[4];
  int crtc_count;
  drm_framebuffer_t *fb_list;

  /* Device state */
  bool suspended;
  bool initialized;

  /* Linked list of devices */
  struct drm_device *next;
} drm_device_t;

/* ===================================================================== */
/* DRM File (Per-Process Context) */
/* ===================================================================== */

typedef struct drm_file {
  struct drm_device *dev;
  void *driver_priv; /* Driver-specific per-file data */

  /* Context (for 3D) */
  uint32_t context_id;
  bool context_valid;

  struct drm_file *next;
} drm_file_t;

/* ===================================================================== */
/* DRM Format Definitions */
/* ===================================================================== */

#define DRM_FORMAT_XRGB8888 0x34325258 /* ['X','R','2','4'] */
#define DRM_FORMAT_ARGB8888 0x34325241 /* ['A','R','2','4'] */
#define DRM_FORMAT_XBGR8888 0x34324258 /* ['X','B','2','4'] */
#define DRM_FORMAT_RGB565 0x36314752   /* ['R','G','1','6'] */

/* ===================================================================== */
/* Public API */
/* ===================================================================== */

/* Core initialization */
int drm_init(void);

/* Device management */
drm_device_t *drm_device_create(pci_device_t *pci, const drm_driver_ops_t *ops);
void drm_device_destroy(drm_device_t *dev);
drm_device_t *drm_get_device(int dev_id);
drm_device_t *drm_get_primary_device(void);

/* GEM operations */
drm_gem_object_t *drm_gem_create(drm_device_t *dev, uint64_t size,
                                 uint32_t flags);
void drm_gem_free(drm_gem_object_t *bo);
drm_gem_object_t *drm_gem_lookup(drm_device_t *dev, uint32_t handle);
int drm_gem_mmap(drm_gem_object_t *bo);
void drm_gem_munmap(drm_gem_object_t *bo);
void drm_gem_ref(drm_gem_object_t *bo);
void drm_gem_unref(drm_gem_object_t *bo);

/* Fence operations */
drm_fence_t *drm_fence_create(drm_device_t *dev);
int drm_fence_wait(drm_fence_t *fence, uint64_t timeout_ns);
bool drm_fence_signaled(drm_fence_t *fence);
void drm_fence_signal(drm_fence_t *fence);

/* Ring buffer operations */
drm_ring_t *drm_ring_create(drm_device_t *dev, int ring_id, uint32_t size);
void drm_ring_destroy(drm_ring_t *ring);
int drm_ring_emit(drm_ring_t *ring, void *data, uint32_t size);
void drm_ring_commit(drm_ring_t *ring);

/* Framebuffer operations */
drm_framebuffer_t *drm_fb_create(drm_device_t *dev, drm_gem_object_t *bo,
                                 uint32_t width, uint32_t height,
                                 uint32_t pitch, uint32_t format);
void drm_fb_destroy(drm_framebuffer_t *fb);

/* Display operations */
int drm_set_mode(drm_device_t *dev, int crtc_id, drm_mode_t *mode);
int drm_page_flip(drm_device_t *dev, int crtc_id, drm_framebuffer_t *fb);

#endif /* DRIVERS_DRM_H */
