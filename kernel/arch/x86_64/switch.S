/*
 * x86_64 Context Switch Implementation
 */

.section .text

/*
 * void switch_context(cpu_context_t *old, cpu_context_t *new)
 * 
 * x86_64 calling convention:
 *   rdi = first argument (old context)
 *   rsi = second argument (new context)
 */
.global switch_context
.type switch_context, @function
switch_context:
    /* Save old context */
    /* Save callee-saved registers */
    mov %rbx, 0(%rdi)      /* rbx */
    mov %rbp, 8(%rdi)      /* rbp */
    mov %r12, 16(%rdi)     /* r12 */
    mov %r13, 24(%rdi)     /* r13 */
    mov %r14, 32(%rdi)     /* r14 */
    mov %r15, 40(%rdi)     /* r15 */
    
    /* Save stack pointer */
    mov %rsp, 48(%rdi)     /* rsp */
    
    /* Save return address (rip) */
    mov (%rsp), %rax
    mov %rax, 56(%rdi)     /* rip */
    
    /* Save flags */
    pushfq
    pop %rax
    mov %rax, 64(%rdi)     /* rflags */
    
    /* Restore new context */
    /* Restore callee-saved registers */
    mov 0(%rsi), %rbx      /* rbx */
    mov 8(%rsi), %rbp      /* rbp */
    mov 16(%rsi), %r12     /* r12 */
    mov 24(%rsi), %r13     /* r13 */
    mov 32(%rsi), %r14     /* r14 */
    mov 40(%rsi), %r15     /* r15 */
    
    /* Restore stack pointer */
    mov 48(%rsi), %rsp     /* rsp */
    
    /* Restore flags */
    mov 64(%rsi), %rax     /* rflags */
    push %rax
    popfq
    
    /* Jump to new instruction pointer */
    mov 56(%rsi), %rax     /* rip */
    jmp *%rax

/*
 * void _task_exit_stub(void)
 * Called when a task exits (internal stub)
 */
.global _task_exit_stub
.type _task_exit_stub, @function
_task_exit_stub:
    /* Disable interrupts */
    cli
    
    /* Call exit_task(0) from sched.c */
    xor %rdi, %rdi      /* exit code = 0 */
    call exit_task
    
    /* Should never reach here */
1:
    hlt
    jmp 1b
