/*
 * vib-OS Kernel - Sandbox Implementation
 *
 * Provides fault-tolerant execution for media decoders and other
 * potentially unsafe code. Uses simplified setjmp/longjmp-style
 * recovery to catch crashes without kernel panic.
 */

#include "../include/sandbox/sandbox.h"
#include "../include/mm/kmalloc.h"
#include "../include/printk.h"
#include "../include/sync/spinlock.h"

/* ===================================================================== */
/* Recovery State Structure */
/* ===================================================================== */

/* Architecture-specific register save area for recovery */
typedef struct sandbox_jmpbuf {
#ifdef ARCH_ARM64
  uint64_t x19, x20, x21, x22, x23, x24, x25, x26, x27, x28, x29;
  uint64_t sp;
  uint64_t lr; /* Return address */
#elif defined(ARCH_X86_64)
  uint64_t rbx, rbp, r12, r13, r14, r15;
  uint64_t rsp;
  uint64_t rip;
#else
  uint64_t placeholder[16];
#endif
} sandbox_jmpbuf_t;

/* ===================================================================== */
/* Per-CPU Sandbox State */
/* ===================================================================== */

/* Current sandbox context (NULL = not in sandbox) */
static sandbox_ctx_t *current_sandbox = NULL;

/* Lock for sandbox operations */
static DEFINE_SPINLOCK(sandbox_lock);

/* ===================================================================== */
/* Architecture-Specific Assembly */
/* ===================================================================== */

#ifdef ARCH_ARM64
/*
 * Save callee-saved registers and return 0
 * On longjmp, returns 1
 */
static inline int sandbox_setjmp(sandbox_jmpbuf_t *buf) {
  int result;
  asm volatile("stp x19, x20, [%1, #0]\n"
               "stp x21, x22, [%1, #16]\n"
               "stp x23, x24, [%1, #32]\n"
               "stp x25, x26, [%1, #48]\n"
               "stp x27, x28, [%1, #64]\n"
               "str x29, [%1, #80]\n"
               "mov x2, sp\n"
               "str x2, [%1, #88]\n"
               "str x30, [%1, #96]\n"
               "mov %0, #0\n"
               : "=r"(result)
               : "r"(buf)
               : "x2", "memory");
  return result;
}

/*
 * Restore callee-saved registers and "return" to setjmp location
 * val becomes the return value of setjmp
 */
static inline void __attribute__((noreturn))
sandbox_longjmp(sandbox_jmpbuf_t *buf, int val) {
  asm volatile("ldp x19, x20, [%0, #0]\n"
               "ldp x21, x22, [%0, #16]\n"
               "ldp x23, x24, [%0, #32]\n"
               "ldp x25, x26, [%0, #48]\n"
               "ldp x27, x28, [%0, #64]\n"
               "ldr x29, [%0, #80]\n"
               "ldr x2, [%0, #88]\n"
               "mov sp, x2\n"
               "ldr x30, [%0, #96]\n"
               "mov x0, %1\n"
               "ret\n"
               :
               : "r"(buf), "r"(val)
               : "x2", "memory");
  __builtin_unreachable();
}

#elif defined(ARCH_X86_64)

static inline int sandbox_setjmp(sandbox_jmpbuf_t *buf) {
  int result;
  asm volatile("movq %%rbx, 0(%1)\n"
               "movq %%rbp, 8(%1)\n"
               "movq %%r12, 16(%1)\n"
               "movq %%r13, 24(%1)\n"
               "movq %%r14, 32(%1)\n"
               "movq %%r15, 40(%1)\n"
               "movq %%rsp, 48(%1)\n"
               "leaq (%%rip), %%rax\n"
               "movq %%rax, 56(%1)\n"
               "xorl %0, %0\n"
               : "=a"(result)
               : "r"(buf)
               : "memory");
  return result;
}

static inline void __attribute__((noreturn))
sandbox_longjmp(sandbox_jmpbuf_t *buf, int val) {
  asm volatile("movq 0(%0), %%rbx\n"
               "movq 8(%0), %%rbp\n"
               "movq 16(%0), %%r12\n"
               "movq 24(%0), %%r13\n"
               "movq 32(%0), %%r14\n"
               "movq 40(%0), %%r15\n"
               "movq 48(%0), %%rsp\n"
               "movq 56(%0), %%rcx\n"
               "movl %1, %%eax\n"
               "jmpq *%%rcx\n"
               :
               : "r"(buf), "r"(val)
               : "memory");
  __builtin_unreachable();
}

#else
/* Fallback - no actual recovery support */
static inline int sandbox_setjmp(sandbox_jmpbuf_t *buf) {
  (void)buf;
  return 0;
}

static inline void __attribute__((noreturn))
sandbox_longjmp(sandbox_jmpbuf_t *buf, int val) {
  (void)buf;
  (void)val;
  /* Can't recover - halt */
  while (1) {
#ifdef ARCH_ARM64
    asm volatile("wfi");
#else
    asm volatile("hlt");
#endif
  }
}
#endif

/* ===================================================================== */
/* Public API Implementation */
/* ===================================================================== */

int sandbox_init(sandbox_ctx_t *ctx, size_t stack_size, size_t result_size) {
  if (!ctx)
    return -1;

  /* Zero initialize */
  for (size_t i = 0; i < sizeof(sandbox_ctx_t); i++) {
    ((uint8_t *)ctx)[i] = 0;
  }

  /* Allocate isolated stack */
  ctx->stack_size = stack_size > 0 ? stack_size : 64 * 1024; /* Default 64KB */
  ctx->stack = kmalloc(ctx->stack_size, 0);
  if (!ctx->stack) {
    printk(KERN_ERR "SANDBOX: Failed to allocate %lu byte stack\n",
           (unsigned long)ctx->stack_size);
    return -1;
  }

  /* Allocate result buffer if needed */
  if (result_size > 0) {
    ctx->result_buffer = kmalloc(result_size, 0);
    if (!ctx->result_buffer) {
      kfree(ctx->stack);
      ctx->stack = NULL;
      return -1;
    }
    ctx->result_size = result_size;
  }

  /* Allocate recovery state */
  ctx->recovery_state = kmalloc(sizeof(sandbox_jmpbuf_t), 0);
  if (!ctx->recovery_state) {
    kfree(ctx->stack);
    if (ctx->result_buffer)
      kfree(ctx->result_buffer);
    return -1;
  }

  ctx->faulted = 0;
  ctx->fault_addr = 0;
  ctx->fault_type = SANDBOX_FAULT_NONE;

  return 0;
}

void sandbox_destroy(sandbox_ctx_t *ctx) {
  if (!ctx)
    return;

  if (ctx->stack) {
    kfree(ctx->stack);
    ctx->stack = NULL;
  }
  if (ctx->result_buffer) {
    kfree(ctx->result_buffer);
    ctx->result_buffer = NULL;
  }
  if (ctx->recovery_state) {
    kfree(ctx->recovery_state);
    ctx->recovery_state = NULL;
  }
}

int sandbox_execute(sandbox_ctx_t *ctx, sandbox_fn_t fn, void *arg) {
  if (!ctx || !fn || !ctx->recovery_state) {
    return -1;
  }

  uint64_t flags = spin_lock_irqsave(&sandbox_lock);

  /* Set as current sandbox */
  sandbox_ctx_t *prev_sandbox = current_sandbox;
  current_sandbox = ctx;
  ctx->faulted = 0;
  ctx->fault_type = SANDBOX_FAULT_NONE;

  spin_unlock_irqrestore(&sandbox_lock, flags);

  sandbox_jmpbuf_t *jmpbuf = (sandbox_jmpbuf_t *)ctx->recovery_state;

  if (sandbox_setjmp(jmpbuf) == 0) {
    /* Normal execution path */
    int result = fn(arg, ctx->result_buffer, ctx->result_size);

    /* Execution completed successfully */
    flags = spin_lock_irqsave(&sandbox_lock);
    current_sandbox = prev_sandbox;
    spin_unlock_irqrestore(&sandbox_lock, flags);

    return result;
  } else {
    /* Returned from fault handler - sandbox crashed */
    flags = spin_lock_irqsave(&sandbox_lock);
    current_sandbox = prev_sandbox;
    spin_unlock_irqrestore(&sandbox_lock, flags);

    printk(KERN_WARNING "SANDBOX: Execution faulted at 0x%llx (type %d)\n",
           (unsigned long long)ctx->fault_addr, (int)ctx->fault_type);

    return -1;
  }
}

void sandbox_handle_fault(uint64_t fault_addr, uint64_t fault_type) {
  sandbox_ctx_t *ctx = current_sandbox;

  if (!ctx || !ctx->recovery_state) {
    /* Not in sandbox - can't recover, let kernel handle it */
    return;
  }

  /* Record fault information */
  ctx->faulted = 1;
  ctx->fault_addr = fault_addr;
  ctx->fault_type = fault_type;

  /* Jump back to sandbox_execute */
  sandbox_longjmp((sandbox_jmpbuf_t *)ctx->recovery_state, 1);
}

int sandbox_is_active(void) { return current_sandbox != NULL; }

sandbox_ctx_t *sandbox_get_current(void) { return current_sandbox; }
