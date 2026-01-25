/*
 * UnixOS Kernel - Scheduler Implementation
 */

#include "sched/sched.h"
#include "mm/pmm.h"
#include "printk.h"

/* ===================================================================== */
/* Static data */
/* ===================================================================== */

/* Global run queue (single CPU for now) */
static struct rq runqueue;

/* Task pool (simple allocation for now) */
#define MAX_TASKS   256
static struct task_struct task_pool[MAX_TASKS];
static int task_pool_index = 0;

/* PID counter */
static pid_t next_pid = 1;

/* Init task (PID 0 / swapper) */
static struct task_struct init_task = {
    .state = TASK_RUNNING,
    .prio = PRIO_DEFAULT,
    .static_prio = PRIO_DEFAULT,
    .nice = 0,
    .pid = 0,
    .tgid = 0,
    .comm = "swapper",
    .flags = PF_KTHREAD | PF_IDLE,
};

/* ===================================================================== */
/* Helper functions */
/* ===================================================================== */

static struct task_struct *alloc_task(void)
{
    if (task_pool_index >= MAX_TASKS) {
        return NULL;
    }
    
    struct task_struct *task = &task_pool[task_pool_index++];
    
    /* Zero initialize */
    char *p = (char *)task;
    for (size_t i = 0; i < sizeof(*task); i++) {
        p[i] = 0;
    }
    
    return task;
}

static void *alloc_stack(size_t size)
{
    /* Allocate kernel stack pages */
    unsigned int order = 0;
    while ((PAGE_SIZE << order) < size && order < 10) {
        order++;
    }
    
    phys_addr_t paddr = pmm_alloc_pages(order);
    if (!paddr) {
        return NULL;
    }
    
    return (void *)paddr;  /* Identity mapped for now */
}

static void enqueue_task(struct task_struct *task)
{
    task->state = TASK_RUNNING;
    
    if (!runqueue.head) {
        runqueue.head = runqueue.tail = task;
        task->next = task->prev = NULL;
    } else {
        task->prev = runqueue.tail;
        task->next = NULL;
        runqueue.tail->next = task;
        runqueue.tail = task;
    }
    
    runqueue.nr_running++;
}

static void dequeue_task(struct task_struct *task)
{
    if (task->prev) {
        task->prev->next = task->next;
    } else {
        runqueue.head = task->next;
    }
    
    if (task->next) {
        task->next->prev = task->prev;
    } else {
        runqueue.tail = task->prev;
    }
    
    task->next = task->prev = NULL;
    
    if (runqueue.nr_running > 0) {
        runqueue.nr_running--;
    }
}

static struct task_struct *pick_next_task(void)
{
    /* Simple round-robin: pick head of queue */
    struct task_struct *next = runqueue.head;
    
    if (!next) {
        /* No runnable tasks - return idle task */
        return runqueue.idle;
    }
    
    return next;
}

/* ===================================================================== */
/* Public functions */
/* ===================================================================== */

void sched_init(void)
{
    printk(KERN_INFO "SCHED: Initializing scheduler\n");
    
    /* Initialize run queue */
    runqueue.current = &init_task;
    runqueue.idle = &init_task;
    runqueue.head = NULL;
    runqueue.tail = NULL;
    runqueue.nr_running = 0;
    runqueue.clock = 0;
    
    printk(KERN_INFO "SCHED: Scheduler initialized\n");
}

void schedule(void)
{
    struct task_struct *prev = runqueue.current;
    struct task_struct *next;
    
    /* Don't schedule if interrupts disabled (should check) */
    
    /* Pick next task */
    next = pick_next_task();
    
    if (next == prev) {
        /* Same task, no switch needed */
        return;
    }
    
    /* Move current to end of queue if still runnable */
    if (prev->state == TASK_RUNNING && prev != runqueue.idle) {
        dequeue_task(prev);
        enqueue_task(prev);
    }
    
    /* Perform context switch */
    runqueue.current = next;
    context_switch(prev, next);
}

int wake_up_process(struct task_struct *task)
{
    if (!task) {
        return 0;
    }
    
    if (task->state == TASK_RUNNING) {
        return 0;  /* Already running */
    }
    
    /* Make runnable */
    task->state = TASK_RUNNING;
    enqueue_task(task);
    
    return 1;
}

struct task_struct *create_task(void (*entry)(void *), void *arg, uint32_t flags)
{
    struct task_struct *task = alloc_task();
    if (!task) {
        printk(KERN_ERR "SCHED: Failed to allocate task\n");
        return NULL;
    }
    
    /* Allocate kernel stack */
    #define KERNEL_STACK_SIZE   (16 * 1024)  /* 16KB */
    void *stack = alloc_stack(KERNEL_STACK_SIZE);
    if (!stack) {
        printk(KERN_ERR "SCHED: Failed to allocate stack\n");
        return NULL;
    }
    
    /* Initialize task */
    task->state = TASK_RUNNING;
    task->prio = PRIO_DEFAULT;
    task->static_prio = PRIO_DEFAULT;
    task->nice = 0;
    task->pid = next_pid++;
    task->tgid = task->pid;
    task->flags = flags;
    task->stack = stack;
    task->stack_size = KERNEL_STACK_SIZE;
    task->parent = runqueue.current;
    
    /* Set up initial CPU context */
    task->cpu_context.sp = (uint64_t)stack + KERNEL_STACK_SIZE;
    task->cpu_context.pc = (uint64_t)entry;
    task->cpu_context.x19 = (uint64_t)arg;  /* Pass arg in callee-saved register */
    
    /* Copy name from parent or use default */
    for (int i = 0; i < TASK_COMM_LEN - 1; i++) {
        task->comm[i] = "task"[i < 4 ? i : 0];
    }
    task->comm[TASK_COMM_LEN - 1] = '\0';
    
    printk(KERN_INFO "SCHED: Created task %d '%s'\n", task->pid, task->comm);
    
    /* Add to run queue */
    enqueue_task(task);
    
    return task;
}

void exit_task(int code)
{
    struct task_struct *current = runqueue.current;
    
    printk(KERN_INFO "SCHED: Task %d exiting with code %d\n", current->pid, code);
    
    current->exit_code = code;
    current->state = TASK_ZOMBIE;
    current->flags |= PF_EXITING;
    
    /* Remove from run queue */
    dequeue_task(current);
    
    /* TODO: Notify parent */
    /* TODO: Reparent children */
    
    /* Schedule another task */
    schedule();
    
    /* Should never reach here */
    while (1) {
#ifdef ARCH_ARM64
        asm volatile("wfi");
#elif defined(ARCH_X86_64) || defined(ARCH_X86)
        asm volatile("hlt");
#endif
    }
}

/* ===================================================================== */
/* Multi-threading Support */
/* ===================================================================== */

pid_t create_thread(void (*entry)(void *), void *arg, void *stack, uint32_t clone_flags)
{
    struct task_struct *parent = runqueue.current;
    struct task_struct *task = alloc_task();
    
    if (!task) {
        printk(KERN_ERR "SCHED: Failed to allocate thread\n");
        return -1;
    }
    
    /* Initialize thread - inherit most things from parent */
    task->state = TASK_RUNNING;
    task->prio = parent->prio;
    task->static_prio = parent->static_prio;
    task->nice = parent->nice;
    task->pid = next_pid++;
    task->tgid = (clone_flags & CLONE_THREAD) ? parent->tgid : task->pid;
    task->flags = PF_THREAD;
    task->parent = parent;
    task->uid = parent->uid;
    task->gid = parent->gid;
    
    /* Copy name with " [thread]" suffix */
    int i;
    for (i = 0; i < TASK_COMM_LEN - 10 && parent->comm[i]; i++) {
        task->comm[i] = parent->comm[i];
    }
    task->comm[i] = '\0';
    
    /* Share memory if CLONE_VM is set */
    if (clone_flags & CLONE_VM) {
        task->mm = parent->mm;
        task->active_mm = parent->active_mm;
        if (task->mm) {
            task->mm->users.counter++;
        }
    } else {
        /* Would need to copy address space - not implemented */
        task->mm = parent->mm;
        task->active_mm = parent->active_mm;
    }
    
    /* Use provided stack or allocate new one */
    if (stack) {
        task->stack = stack;
        task->stack_size = 0;  /* External stack, don't free */
        task->cpu_context.sp = (uint64_t)stack;
    } else {
        #define KERNEL_STACK_SIZE   (16 * 1024)
        task->stack = alloc_stack(KERNEL_STACK_SIZE);
        if (!task->stack) {
            printk(KERN_ERR "SCHED: Failed to allocate thread stack\n");
            return -1;
        }
        task->stack_size = KERNEL_STACK_SIZE;
        task->cpu_context.sp = (uint64_t)task->stack + KERNEL_STACK_SIZE;
    }
    
    task->cpu_context.pc = (uint64_t)entry;
    task->cpu_context.x19 = (uint64_t)arg;
    
    printk(KERN_INFO "SCHED: Created thread %d (tgid=%d) for '%s'\n", 
           task->pid, task->tgid, parent->comm);
    
    /* Add to run queue */
    enqueue_task(task);
    
    return task->pid;
}

struct task_struct *get_task_by_pid(pid_t pid)
{
    /* Check init task */
    if (init_task.pid == pid) {
        return &init_task;
    }
    
    /* Search task pool */
    for (int i = 0; i < task_pool_index; i++) {
        if (task_pool[i].pid == pid && task_pool[i].state != TASK_DEAD) {
            return &task_pool[i];
        }
    }
    
    return NULL;
}

int sched_kill_task(pid_t pid)
{
    struct task_struct *task = get_task_by_pid(pid);
    
    if (!task) {
        return -3;  /* ESRCH - No such process */
    }
    
    /* Can't kill init or idle */
    if (task->pid == 0 || (task->flags & PF_IDLE)) {
        return -1;  /* EPERM - Operation not permitted */
    }
    
    /* Mark for termination */
    printk(KERN_INFO "SCHED: Killing task %d '%s'\n", pid, task->comm);
    
    task->flags |= PF_EXITING;
    task->pending_signals |= (1ULL << 9);  /* SIGKILL */
    
    /* If sleeping, wake it up */
    if (task->state == TASK_INTERRUPTIBLE || task->state == TASK_UNINTERRUPTIBLE) {
        task->state = TASK_RUNNING;
        enqueue_task(task);
    }
    
    return 0;
}

struct task_struct *get_current(void)
{
    return runqueue.current;
}

void context_switch(struct task_struct *prev, struct task_struct *next)
{
    /* Switch address space if needed */
    if (next->mm && next->mm != prev->mm) {
        vmm_switch_address_space(next->mm);
    }
    next->active_mm = next->mm ? next->mm : prev->active_mm;
    
    /* Switch CPU context */
    cpu_switch_to(prev, next);
}

/* ===================================================================== */
/* Context switch assembly helper - defined in switch.S */
/* ===================================================================== */

/* Placeholder - actual implementation is in assembly */
void __attribute__((weak)) cpu_switch_to(struct task_struct *prev, struct task_struct *next)
{
    (void)prev;
    (void)next;
    /* This is overridden by assembly implementation */
}
