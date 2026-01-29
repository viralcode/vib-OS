/*
 * VibCode x64 - Simple Kernel Memory Allocator
 * 
 * A simple first-fit allocator using a static heap.
 */

#include "../include/kmalloc.h"
#include "../include/string.h"

/* Heap configuration - 64MB heap */
#define HEAP_SIZE (64 * 1024 * 1024)
#define MIN_BLOCK_SIZE 64
#define BLOCK_MAGIC 0xDEADBEEF

/* Block header */
typedef struct block {
  uint32_t magic;
  uint32_t size;      /* Size including header */
  uint32_t free;      /* 1 if free, 0 if used */
  struct block *next;
  struct block *prev;
} block_t;

/* Static heap */
static uint8_t heap_memory[HEAP_SIZE] __attribute__((aligned(16)));
static block_t *free_list = NULL;
static int heap_initialized = 0;
static size_t heap_used = 0;

/* Initialize heap */
void kmalloc_init(void) {
  if (heap_initialized)
    return;

  /* Create initial free block spanning entire heap */
  free_list = (block_t *)heap_memory;
  free_list->magic = BLOCK_MAGIC;
  free_list->size = HEAP_SIZE;
  free_list->free = 1;
  free_list->next = NULL;
  free_list->prev = NULL;

  heap_initialized = 1;
  heap_used = sizeof(block_t);
}

/* Find a free block of at least 'size' bytes */
static block_t *find_free_block(size_t size) {
  block_t *block = free_list;
  
  while (block) {
    if (block->free && block->size >= size + sizeof(block_t)) {
      return block;
    }
    block = block->next;
  }
  
  return NULL;
}

/* Split a block if it's large enough */
static void split_block(block_t *block, size_t size) {
  size_t total_needed = size + sizeof(block_t);
  
  /* Only split if remaining space is useful */
  if (block->size >= total_needed + MIN_BLOCK_SIZE + sizeof(block_t)) {
    /* Create new block from remainder */
    block_t *new_block = (block_t *)((uint8_t *)block + total_needed);
    new_block->magic = BLOCK_MAGIC;
    new_block->size = block->size - total_needed;
    new_block->free = 1;
    new_block->next = block->next;
    new_block->prev = block;
    
    if (block->next) {
      block->next->prev = new_block;
    }
    
    block->size = total_needed;
    block->next = new_block;
  }
}

/* Merge adjacent free blocks */
static void coalesce(block_t *block) {
  /* Merge with next block if free */
  if (block->next && block->next->free) {
    block->size += block->next->size;
    block->next = block->next->next;
    if (block->next) {
      block->next->prev = block;
    }
  }
  
  /* Merge with previous block if free */
  if (block->prev && block->prev->free) {
    block->prev->size += block->size;
    block->prev->next = block->next;
    if (block->next) {
      block->next->prev = block->prev;
    }
  }
}

/* Allocate memory */
void *kmalloc(size_t size) {
  if (!heap_initialized) {
    kmalloc_init();
  }
  
  if (size == 0) {
    return NULL;
  }
  
  /* Align size to 16 bytes */
  size = (size + 15) & ~15;
  
  /* Find a free block */
  block_t *block = find_free_block(size);
  if (!block) {
    return NULL; /* Out of memory */
  }
  
  /* Split block if too large */
  split_block(block, size);
  
  /* Mark as used */
  block->free = 0;
  heap_used += block->size;
  
  /* Return pointer to data (after header) */
  return (void *)((uint8_t *)block + sizeof(block_t));
}

/* Allocate and zero memory */
void *kzalloc(size_t size) {
  void *ptr = kmalloc(size);
  if (ptr) {
    memset(ptr, 0, size);
  }
  return ptr;
}

/* Reallocate memory */
void *krealloc(void *ptr, size_t new_size) {
  if (!ptr) {
    return kmalloc(new_size);
  }
  
  if (new_size == 0) {
    kfree(ptr);
    return NULL;
  }
  
  /* Get current block */
  block_t *block = (block_t *)((uint8_t *)ptr - sizeof(block_t));
  size_t old_size = block->size - sizeof(block_t);
  
  /* If new size fits in current block, just return */
  if (new_size <= old_size) {
    return ptr;
  }
  
  /* Allocate new block and copy */
  void *new_ptr = kmalloc(new_size);
  if (new_ptr) {
    memcpy(new_ptr, ptr, old_size);
    kfree(ptr);
  }
  
  return new_ptr;
}

/* Free memory */
void kfree(void *ptr) {
  if (!ptr) {
    return;
  }
  
  /* Get block header */
  block_t *block = (block_t *)((uint8_t *)ptr - sizeof(block_t));
  
  /* Validate magic */
  if (block->magic != BLOCK_MAGIC) {
    return; /* Invalid pointer */
  }
  
  /* Mark as free */
  block->free = 1;
  heap_used -= block->size;
  
  /* Merge adjacent free blocks */
  coalesce(block);
}

/* Get heap statistics */
size_t kmalloc_get_used(void) {
  return heap_used;
}

size_t kmalloc_get_free(void) {
  return HEAP_SIZE - heap_used;
}
