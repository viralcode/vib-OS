/*
 * vib-OS Kernel - Sandbox Execution Framework
 *
 * Provides fault-tolerant execution environment for untrusted code
 * such as media decoders. If the sandboxed code crashes, the kernel
 * recovers gracefully instead of panicking.
 */

#ifndef _SANDBOX_H
#define _SANDBOX_H

#include "../types.h"

/* Sandbox execution context */
typedef struct sandbox_ctx {
  void *stack; /* Isolated stack for sandbox */
  size_t stack_size;
  void *result_buffer; /* Output buffer for results */
  size_t result_size;
  int faulted;         /* Set to 1 if sandbox crashed */
  uint64_t fault_addr; /* Faulting address if crashed */
  uint64_t fault_type; /* Type of fault */
  /* Recovery state - architecture specific */
  void *recovery_state;
} sandbox_ctx_t;

/* Fault types */
#define SANDBOX_FAULT_NONE 0
#define SANDBOX_FAULT_ACCESS 1   /* Memory access violation */
#define SANDBOX_FAULT_ALIGN 2    /* Alignment fault */
#define SANDBOX_FAULT_OVERFLOW 3 /* Stack overflow */
#define SANDBOX_FAULT_DIV0 4     /* Division by zero */
#define SANDBOX_FAULT_UNKNOWN 255

/* Initialize a sandbox context
 * stack_size: Size of isolated stack (recommend 64KB for media)
 * result_size: Size of result buffer (can be 0 if result returned directly)
 * Returns: 0 on success, -1 on failure (out of memory)
 */
int sandbox_init(sandbox_ctx_t *ctx, size_t stack_size, size_t result_size);

/* Destroy a sandbox context and free resources */
void sandbox_destroy(sandbox_ctx_t *ctx);

/* Sandbox function prototype
 * arg: User-provided argument
 * result: Output buffer (ctx->result_buffer)
 * result_size: Size of output buffer (ctx->result_size)
 * Returns: 0 on success, negative on error
 */
typedef int (*sandbox_fn_t)(void *arg, void *result, size_t result_size);

/* Execute a function in the sandbox
 * ctx: Initialized sandbox context
 * fn: Function to execute
 * arg: Argument to pass to function
 * Returns: Return value of fn, or -1 if sandbox faulted (check ctx->faulted)
 */
int sandbox_execute(sandbox_ctx_t *ctx, sandbox_fn_t fn, void *arg);

/* Called by fault handler when sandbox crashes (internal use) */
void sandbox_handle_fault(uint64_t fault_addr, uint64_t fault_type);

/* Check if we're currently in a sandbox */
int sandbox_is_active(void);

/* Get current sandbox context (for fault handler) */
sandbox_ctx_t *sandbox_get_current(void);

#endif /* _SANDBOX_H */
