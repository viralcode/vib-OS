/*
 * x86 32-bit Context Switch Implementation
 */

.section .text
.code32

/*
 * void switch_context(cpu_context_t *old, cpu_context_t *new)
 * 
 * x86 32-bit calling convention (cdecl):
 *   Arguments on stack: [esp+4] = old, [esp+8] = new
 */
.global switch_context
.type switch_context, @function
switch_context:
    /* Get arguments from stack */
    movl 4(%esp), %eax  /* old context */
    movl 8(%esp), %edx  /* new context */
    
    /* Save old context */
    movl %ebx, 0(%eax)   /* ebx */
    movl %esi, 4(%eax)   /* esi */
    movl %edi, 8(%eax)   /* edi */
    movl %ebp, 12(%eax)  /* ebp */
    movl %esp, 16(%eax)  /* esp */
    
    /* Save return address (eip) */
    movl (%esp), %ecx
    movl %ecx, 20(%eax)  /* eip */
    
    /* Save flags */
    pushfl
    popl %ecx
    movl %ecx, 24(%eax)  /* eflags */
    
    /* Restore new context */
    movl 0(%edx), %ebx   /* ebx */
    movl 4(%edx), %esi   /* esi */
    movl 8(%edx), %edi   /* edi */
    movl 12(%edx), %ebp  /* ebp */
    movl 16(%edx), %esp  /* esp */
    
    /* Restore flags */
    movl 24(%edx), %ecx  /* eflags */
    pushl %ecx
    popfl
    
    /* Jump to new instruction pointer */
    movl 20(%edx), %ecx  /* eip */
    jmp *%ecx

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
    pushl $0            /* exit code = 0 */
    call exit_task
    addl $4, %esp
    
    /* Should never reach here */
1:
    hlt
    jmp 1b
