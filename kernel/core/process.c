/*
 * Vib-OS Process Management (ported from VibeOS)
 *
 * Preemptive multitasking - timer IRQ forces context switches.
 * Programs run in kernel space and call kernel functions directly.
 * No memory protection, but full preemption via timer interrupt.
 */

#include "process.h"
#include "../include/arch/arch.h"
#include "../include/fs/vfs_compat.h"
#include "../include/loader/elf.h"
#include "../include/mm/kmalloc.h"
#include "../include/printk.h"
#include "../include/sync/spinlock.h"
#include "../include/mm/aslr.h"

/* Forward declare strncpy and strlen from our kernel */
extern char *strncpy(char *dst, const char *src, size_t n);
extern size_t strlen(const char *s);
extern void *memset(void *s, int c, size_t n);

/* Fixed program load base address (after heap area at 0x42800000) */
#define PROGRAM_LOAD_BASE 0x44000000ULL

/* Use printk instead of printf, and kmalloc/kfree instead of malloc/free */
#define printf printk
#define malloc(size) _kmalloc((size), 0)
#define free kfree
#define vfs_read vfs_read_compat

/* Get kapi from launcher.c which has all the real functions */
struct kapi;
typedef struct kapi kapi_t;
extern kapi_t *kapi_get(void);

// Process table
static process_t proc_table[MAX_PROCESSES];
static int current_pid = -1; // -1 means kernel/shell is running
static int next_pid = 1;

// Spinlock protecting process table access
static DEFINE_SPINLOCK(proc_table_lock);

// Current process pointer - used by IRQ handler for preemption
// NULL means kernel is running (no process to save to)
process_t *current_process = NULL;

// Kernel context - saved when switching from kernel to a process
// This allows us to return to kernel (e.g., desktop running via process_exec)
// Global (not static) so vectors.S can access it for kernel->process IRQ
// switches
cpu_context_t kernel_context;

// Program load address - grows upward as we load programs
// Set dynamically based on heap_end
static uint64_t program_base = 0;
static uint64_t next_load_addr = 0;

// Align to 64KB boundary for cleaner loading
#define ALIGN_64K(x) (((x) + 0xFFFF) & ~0xFFFFULL)

// Program entry point signature
typedef int (*program_entry_t)(kapi_t *api, int argc, char **argv);

// Forward declarations
static void process_entry_wrapper(void);
static void kill_children(int parent_pid);

void process_init(void) {
  // Clear process table
  for (int i = 0; i < MAX_PROCESSES; i++) {
    proc_table[i].state = PROC_STATE_FREE;
    proc_table[i].pid = 0;
    // Also clear context to prevent garbage
    memset(&proc_table[i].context, 0, sizeof(cpu_context_t));
  }
  current_pid = -1;
  current_process = NULL;
  next_pid = 1;

  // Programs load right after the heap
  program_base = ALIGN_64K(PROGRAM_LOAD_BASE);
  next_load_addr = program_base;

  printf("[PROC] Process subsystem initialized (max %d processes)\n",
         MAX_PROCESSES);
  printf("[PROC] Program load area: 0x%lx+\n", program_base);
  printf("[PROC] kernel_context at: 0x%lx\n", (uint64_t)&kernel_context);
}

// Find a free slot in the process table (caller must hold proc_table_lock)
static int find_free_slot_unlocked(void) {
  for (int i = 0; i < MAX_PROCESSES; i++) {
    if (proc_table[i].state == PROC_STATE_FREE) {
      return i;
    }
  }
  return -1;
}

process_t *process_current(void) {
  if (current_pid < 0)
    return NULL;
  return &proc_table[current_pid];
}

process_t *process_get(int pid) {
  uint64_t flags = spin_lock_irqsave(&proc_table_lock);
  for (int i = 0; i < MAX_PROCESSES; i++) {
    if (proc_table[i].pid == pid && proc_table[i].state != PROC_STATE_FREE) {
      spin_unlock_irqrestore(&proc_table_lock, flags);
      return &proc_table[i];
    }
  }
  spin_unlock_irqrestore(&proc_table_lock, flags);
  return NULL;
}

// Get pointer to current_process pointer (for assembly IRQ handler)
process_t **process_get_current_ptr(void) { return &current_process; }

int process_count_ready(void) {
  uint64_t flags = spin_lock_irqsave(&proc_table_lock);
  int count = 0;
  for (int i = 0; i < MAX_PROCESSES; i++) {
    if (proc_table[i].state == PROC_STATE_READY ||
        proc_table[i].state == PROC_STATE_RUNNING) {
      count++;
    }
  }
  spin_unlock_irqrestore(&proc_table_lock, flags);
  return count;
}

int process_get_info(int index, char *name, int name_size, int *state) {
  if (index < 0 || index >= MAX_PROCESSES)
    return 0;
  process_t *p = &proc_table[index];
  if (p->state == PROC_STATE_FREE)
    return 0;

  // Copy name
  if (name && name_size > 0) {
    int len = strlen(p->name);
    if (len >= name_size)
      len = name_size - 1;
    for (int i = 0; i < len; i++)
      name[i] = p->name[i];
    name[len] = '\0';
  }

  // Return state
  if (state)
    *state = (int)p->state;

  return 1;
}

// Create a new process (load the binary but don't start it)
int process_create(const char *path, int argc, char **argv) {
  (void)argc;
  (void)argv;

  // Find free slot (with locking)
  uint64_t flags = spin_lock_irqsave(&proc_table_lock);
  int slot = find_free_slot_unlocked();
  if (slot < 0) {
    spin_unlock_irqrestore(&proc_table_lock, flags);
    printf("[PROC] No free process slots\n");
    return -1;
  }
  // Reserve the slot immediately
  proc_table[slot].state = PROC_STATE_READY;
  proc_table[slot].pid = next_pid++;
  spin_unlock_irqrestore(&proc_table_lock, flags);

  // Look up file
  vfs_node_t *file = vfs_lookup(path);
  if (!file) {
    printf("[PROC] File not found: %s\n", path);
    return -1;
  }

  if (vfs_is_dir(file)) {
    printf("[PROC] Cannot exec directory: %s\n", path);
    return -1;
  }

  size_t size = file->size;
  if (size == 0) {
    printf("[PROC] File is empty: %s\n", path);
    return -1;
  }

  // Read the ELF file
  char *data = malloc(size);
  if (!data) {
    printf("[PROC] Out of memory reading %s\n", path);
    return -1;
  }

  int bytes = vfs_read(file, data, size, 0);
  if (bytes != (int)size) {
    printf("[PROC] Failed to read %s\n", path);
    free(data);
    return -1;
  }

  // Calculate how much memory the program needs
  uint64_t prog_size = elf_calc_size(data, size);
  if (prog_size == 0) {
    int err = elf_validate(data, size);
    printf("[PROC] Invalid ELF: %s (err=%d, size=%d)\n", path, err, (int)size);
    uint8_t *b = (uint8_t *)data;
    printf("[PROC] Header: %02x %02x %02x %02x %02x %02x %02x %02x\n", b[0],
           b[1], b[2], b[3], b[4], b[5], b[6], b[7]);
    free(data);
    return -1;
  }

  // Align load address with ASLR randomization
  uint64_t aslr_offset = aslr_exec_offset();
  uint64_t load_addr = ALIGN_64K(next_load_addr + aslr_offset);

  // Load the ELF at this address
  elf_load_info_t info;
  if (elf_load_at(data, size, load_addr, &info) != 0) {
    printf("[PROC] Failed to load ELF: %s\n", path);
    free(data);
    return -1;
  }

  free(data);

  // Update next load address for future programs
  next_load_addr = ALIGN_64K(load_addr + info.load_size + 0x10000);

  // Set up process structure
  process_t *proc = &proc_table[slot];
  proc->pid = next_pid++;
  strncpy(proc->name, path, PROCESS_NAME_MAX - 1);
  proc->name[PROCESS_NAME_MAX - 1] = '\0';
  proc->state = PROC_STATE_READY;
  proc->load_base = info.load_base;
  proc->load_size = info.load_size;
  proc->entry = info.entry;
  proc->parent_pid = current_pid;
  proc->exit_status = 0;

  // Allocate stack
  proc->stack_size = PROCESS_STACK_SIZE;
  proc->stack_base = malloc(proc->stack_size);
  if (!proc->stack_base) {
    printf("[PROC] Failed to allocate stack\n");
    proc->state = PROC_STATE_FREE;
    return -1;
  }

  // Initialize context
  // Stack grows down, SP starts at top (aligned to 16 bytes)
  uint64_t stack_top =
      ((uint64_t)proc->stack_base + proc->stack_size) & ~0xFULL;

  // Set up initial context for preemptive scheduling
  // pc = entry wrapper, parameters in callee-saved registers x19-x22
  memset(&proc->context, 0, sizeof(cpu_context_t));
  arch_context_set_sp(&proc->context, stack_top);
  arch_context_set_pc(&proc->context,
                      (uint64_t)process_entry_wrapper); // Start here

#ifdef ARCH_ARM64
  arch_context_set_flags(&proc->context,
                         0x3c5); // EL1h, DAIF masked (IRQs disabled initially)
  // Pass arguments via callee-saved registers
  proc->context.x[19] = proc->entry;          // x19 = entry point
  proc->context.x[20] = (uint64_t)kapi_get(); // x20 = kapi pointer
  proc->context.x[21] = (uint64_t)argc;       // x21 = argc
  proc->context.x[22] = (uint64_t)argv;       // x22 = argv
#elif defined(ARCH_X86_64)
  arch_context_set_flags(&proc->context, 0x202); // IF (interrupts enabled)
  // Pass arguments via callee-saved registers (similar to ARM64)
  proc->context.r12 = proc->entry;          // r12 = entry point
  proc->context.r13 = (uint64_t)kapi_get(); // r13 = kapi pointer
  proc->context.r14 = (uint64_t)argc;       // r14 = argc
  proc->context.r15 = (uint64_t)argv;       // r15 = argv
#elif defined(ARCH_X86)
  arch_context_set_flags(&proc->context, 0x202); // IF (interrupts enabled)
  // x86 32-bit: pass via stack or registers (TBD)
#endif

  // printf("[PROC] Created process '%s' pid=%d at 0x%lx-0x%lx (slot %d)\n",
  //        proc->name, proc->pid, proc->load_base, proc->load_base +
  //        proc->load_size, slot);
  // printf("[PROC] Stack at 0x%lx-0x%lx\n",
  //        (uint64_t)proc->stack_base, (uint64_t)proc->stack_base +
  //        proc->stack_size);

  return proc->pid;
}

// Entry wrapper - called when a new process is switched to for the first time
// Architecture-specific implementation
#ifdef ARCH_ARM64
// Parameters passed in callee-saved registers x19-x22 (preserved across context
// switch) x19 = entry, x20 = kapi, x21 = argc, x22 = argv
static void __attribute__((naked)) process_entry_wrapper(void) {
  asm volatile("mov x0, x20\n"     // x0 = kapi
               "mov x1, x21\n"     // x1 = argc
               "mov x2, x22\n"     // x2 = argv
               "blr x19\n"         // Call entry(kapi, argc, argv)
               "bl process_exit\n" // Exit with return value
               "1: b 1b\n"         // Should never reach here
               ::
                   : "memory");
}
#elif defined(ARCH_X86_64)
// Parameters passed in callee-saved registers r12-r15
// r12 = entry, r13 = kapi, r14 = argc, r15 = argv
static void __attribute__((naked)) process_entry_wrapper(void) {
  asm volatile("movq %%r13, %%rdi\n"  // rdi = kapi (1st arg)
               "movq %%r14, %%rsi\n"  // rsi = argc (2nd arg)
               "movq %%r15, %%rdx\n"  // rdx = argv (3rd arg)
               "callq *%%r12\n"       // Call entry(kapi, argc, argv)
               "movq %%rax, %%rdi\n"  // rdi = exit status
               "callq process_exit\n" // Exit
               "1: jmp 1b\n"          // Should never reach here
               ::
                   : "memory");
}
#elif defined(ARCH_X86)
// x86 32-bit implementation (TBD)
static void process_entry_wrapper(void) {
  // TODO: Implement for x86
  process_exit(0);
}
#endif

// Start a process (make it runnable)
int process_start(int pid) {
  process_t *proc = process_get(pid);
  if (!proc)
    return -1;

  if (proc->state != PROC_STATE_READY) {
    printf("[PROC] Process %d not ready (state=%d)\n", pid, proc->state);
    return -1;
  }

  printf("[PROC] Started '%s' pid=%d\n", proc->name, pid);
  return 0; // Already ready, scheduler will pick it up
}

// Exit current process
void process_exit(int status) {
  // Disable IRQs during exit to prevent race with preemption
  arch_irq_disable();

  if (current_pid < 0) {
    printf("[PROC] Exit called with no current process!\n");
    arch_irq_enable();
    return;
  }

  int slot = current_pid;
  process_t *proc = &proc_table[slot];
  printf("[PROC] Process '%s' (pid %d) exited with status %d\n", proc->name,
         proc->pid, status);

  // Kill all children of this process before exiting
  kill_children(proc->pid);

  proc->exit_status = status;
  proc->state = PROC_STATE_ZOMBIE;

  // Free stack - but we're still on it! Don't free yet.
  // The stack will be freed when the slot is reused.

  // Mark slot as free (simple cleanup for now)
  proc->state = PROC_STATE_FREE;

  // We're done with this process - switch back to kernel context
  // This MUST not return - we context switch away
  current_pid = -1;
  current_process = NULL;

  // Debug: verify kernel_context before switching
  printf("[PROC] Switching to kernel_context: pc=0x%llx sp=0x%llx\n",
         (unsigned long long)arch_context_get_pc(&kernel_context),
         (unsigned long long)arch_context_get_sp(&kernel_context));

  // Sanity check kernel_context
  // Note: kernel code is in flash at 0x0, stack is near 0x5f000000
  if (arch_context_get_pc(&kernel_context) == 0 ||
      arch_context_get_sp(&kernel_context) == 0) {
    printf("[PROC] ERROR: kernel_context appears corrupted!\n");
    printf(
        "[PROC] This indicates memory corruption during process execution\n");
    while (1)
      ; // Hang instead of crashing
  }

  // Switch directly back to kernel context
  // This will resume in process_exec_args() or process_schedule()
  // wherever the kernel was waiting
  // IRQs will be re-enabled when kernel re-enables them
  switch_context(&proc->context, &kernel_context);

  // Should never reach here
  printf("[PROC] ERROR: process_exit returned!\n");
  while (1)
    ;
}

// Yield - voluntarily give up CPU
void process_yield(void) {
  if (current_pid >= 0) {
    // Mark current process as ready
    process_t *proc = &proc_table[current_pid];
    proc->state = PROC_STATE_READY;
  }
  // Always try to schedule - even from kernel context
  // This lets programs started via process_exec() yield to spawned children
  process_schedule();
}

// Simple round-robin scheduler (for voluntary transitions like process_exec)
void process_schedule(void) {
  // Disable IRQs during scheduling to prevent race with preemption
  arch_irq_disable();

  int old_pid = current_pid;
  process_t *old_proc = (old_pid >= 0) ? &proc_table[old_pid] : NULL;

  // Find next runnable process (round-robin)
  int start = (old_pid >= 0) ? old_pid + 1 : 0;
  int next = -1;

  for (int i = 0; i < MAX_PROCESSES; i++) {
    int idx = (start + i) % MAX_PROCESSES;
    if (proc_table[idx].state == PROC_STATE_READY) {
      next = idx;
      break;
    }
  }

  if (next < 0) {
    // No runnable processes
    if (old_pid >= 0 && old_proc->state == PROC_STATE_RUNNING) {
      // Current process still running, keep it
      arch_irq_enable();
      return;
    }
    // Return to kernel (if we were in a process, switch back to kernel)
    if (old_pid >= 0) {
      current_pid = -1;
      current_process = NULL;
      switch_context(&old_proc->context, &kernel_context);
      // When we return here, IRQs will be re-enabled below
    }
    // Already in kernel with nothing to run - sleep until next interrupt
    arch_irq_enable();
    arch_idle();
    return;
  }

  if (next == old_pid && old_proc && old_proc->state == PROC_STATE_RUNNING) {
    // Same process and it's running - nothing to switch
    arch_irq_enable();
    return;
  }

  if (next == old_pid && old_proc && old_proc->state == PROC_STATE_READY) {
    // Process yielded but it's the only one - sleep until interrupt
    old_proc->state = PROC_STATE_RUNNING;
    arch_irq_enable();
    arch_idle();
    return;
  }

  // Switch to new process
  process_t *new_proc = &proc_table[next];

  if (old_proc && old_proc->state == PROC_STATE_RUNNING) {
    old_proc->state = PROC_STATE_READY;
  }

  new_proc->state = PROC_STATE_RUNNING;
  current_pid = next;
  current_process = new_proc;

  // Context switch!
  // If old_pid == -1, we're switching FROM kernel context
  // IRQs stay disabled - new process will enable them (entry_wrapper or return
  // path)
  cpu_context_t *old_ctx =
      (old_pid >= 0) ? &old_proc->context : &kernel_context;

  // Debug: if switching from kernel, verify kernel_context after we return
  int was_kernel = (old_pid < 0);

  switch_context(old_ctx, &new_proc->context);

  // We return here when someone switches back to us
  // Verify kernel_context wasn't corrupted during process execution
  if (was_kernel) {
    if (arch_context_get_pc(&kernel_context) < 0x40000000 ||
        arch_context_get_sp(&kernel_context) < 0x40000000) {
      printf("[PROC] WARNING: kernel_context corrupted after process ran!\n");
      printf("[PROC] pc=0x%llx sp=0x%llx\n",
             (unsigned long long)arch_context_get_pc(&kernel_context),
             (unsigned long long)arch_context_get_sp(&kernel_context));
    }
  }

  arch_irq_enable(); // Re-enable IRQs
}

// Execute and wait - creates a real process and waits for it to finish
int process_exec_args(const char *path, int argc, char **argv) {
  // Create the process
  int pid = process_create(path, argc, argv);
  if (pid < 0) {
    return pid; // Error already printed
  }

  // Start it
  process_start(pid);

  // Find the slot for this process
  int slot = -1;
  for (int i = 0; i < MAX_PROCESSES; i++) {
    if (proc_table[i].pid == pid) {
      slot = i;
      break;
    }
  }

  if (slot < 0) {
    printf("[PROC] exec: process disappeared?\n");
    return -1;
  }

  // Wait for it to finish by yielding until it's done
  // The process is READY, we need to run the scheduler to let it execute
  while (proc_table[slot].state != PROC_STATE_FREE &&
         proc_table[slot].state != PROC_STATE_ZOMBIE) {
    process_schedule();
  }

  int result = proc_table[slot].exit_status;
  printf("[PROC] Process '%s' (pid %d) finished with status %d\n", path, pid,
         result);
  return result;
}

int process_exec(const char *path) {
  char *argv[1] = {(char *)path};
  return process_exec_args(path, 1, argv);
}

// Called from IRQ handler for preemptive scheduling
// Just updates current_process - IRQ handler does the actual context switch
void process_schedule_from_irq(void) {
  // Check how many processes are ready to run
  int ready_count = process_count_ready();

  // If kernel is running (current_pid == -1), we should switch to ANY ready
  // process If a process is running, we only switch if there's another ready
  // process
  if (current_pid >= 0 && ready_count <= 1) {
    return; // Only one process, no point switching
  }
  if (current_pid < 0 && ready_count == 0) {
    return; // Kernel running, no processes to switch to
  }

  // Find next runnable process (round-robin)
  int old_slot = current_pid;
  int start = (old_slot >= 0) ? old_slot + 1 : 0;

  for (int i = 0; i < MAX_PROCESSES; i++) {
    int idx = (start + i) % MAX_PROCESSES;
    if (proc_table[idx].state == PROC_STATE_READY) {
      // Found a different process to switch to
      if (idx != old_slot) {
        // Safety check: verify process has valid context
        process_t *new_proc = &proc_table[idx];
        if (arch_context_get_sp(&new_proc->context) == 0 ||
            arch_context_get_pc(&new_proc->context) == 0) {
          continue; // Skip invalid process
        }

        // Mark old process as ready (it was running)
        if (old_slot >= 0 && proc_table[old_slot].state == PROC_STATE_RUNNING) {
          proc_table[old_slot].state = PROC_STATE_READY;
        }

        // Switch to new process
        proc_table[idx].state = PROC_STATE_RUNNING;
        current_pid = idx;
        current_process = new_proc;

        // Memory barrier to ensure current_process is visible to IRQ handler
        arch_dsb();
      }
      return;
    }
  }
}

// Kill all children of a process (iterative to prevent stack overflow)
static void kill_children(int parent_pid) {
  // Use a work stack to avoid recursion - max depth is MAX_PROCESSES
  int work_stack[MAX_PROCESSES];
  int stack_top = 0;

  // Push initial parent
  work_stack[stack_top++] = parent_pid;

  while (stack_top > 0) {
    int current_parent = work_stack[--stack_top];

    // Find and process all children of current_parent
    for (int i = 0; i < MAX_PROCESSES; i++) {
      if (proc_table[i].state != PROC_STATE_FREE &&
          proc_table[i].parent_pid == current_parent) {
        int child_pid = proc_table[i].pid;

        // Push child to stack so its children get processed too
        if (stack_top < MAX_PROCESSES) {
          work_stack[stack_top++] = child_pid;
        }

        // Kill this child (skip if it's current process)
        if (i != current_pid) {
          printf("[PROC] Killing child '%s' (pid %d, parent %d)\n",
                 proc_table[i].name, child_pid, current_parent);
          if (proc_table[i].stack_base) {
            free(proc_table[i].stack_base);
            proc_table[i].stack_base = NULL;
          }
          proc_table[i].state = PROC_STATE_FREE;
          proc_table[i].pid = 0;
        }
      }
    }
  }
}

// Kill a process by PID
int process_kill(int pid) {
  // Don't allow killing kernel (pid would be invalid anyway)
  if (pid <= 0) {
    printf("[PROC] Cannot kill pid %d\n", pid);
    return -1;
  }

  // Find the process
  int slot = -1;
  for (int i = 0; i < MAX_PROCESSES; i++) {
    if (proc_table[i].pid == pid && proc_table[i].state != PROC_STATE_FREE) {
      slot = i;
      break;
    }
  }

  if (slot < 0) {
    printf("[PROC] Process %d not found\n", pid);
    return -1;
  }

  process_t *proc = &proc_table[slot];

  // Don't allow killing the current process this way - use exit() instead
  if (slot == current_pid) {
    printf("[PROC] Cannot kill current process (use exit)\n");
    return -1;
  }

  printf("[PROC] Killing process '%s' (pid %d)\n", proc->name, pid);

  // First kill all children of this process
  kill_children(pid);

  // Free the process memory
  if (proc->stack_base) {
    free(proc->stack_base);
    proc->stack_base = NULL;
  }

  // Mark slot as free
  proc->state = PROC_STATE_FREE;
  proc->pid = 0;

  return 0;
}
