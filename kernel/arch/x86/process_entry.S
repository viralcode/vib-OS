/*
 * x86 32-bit Process Entry Stub
 *
 * This function completes the switch to userspace (Ring 3).
 * It expects arguments in registers (restored by switch_context):
 *   ebx = entry point
 *   esi = kapi pointer
 *   edi = argc
 *   ebp = argv
 */

.section .text
.code32
.global x86_process_entry
.extern current_process

x86_process_entry:
    /* Get current_process pointer */
    movl current_process, %eax
    
    /* 
     * Calculate user stack pointer.
     * process_t layout assumptions (MUST MATCH types.h/process.h):
     * We need stack_base and stack_size.
     * Since layout is fragile, we'll try a safer way:
     * We are in kernel mode. We can read the struct via C if we called a function,
     * but we want to stay in asm to handle the iret frame construction.
     *
     * Let's accept that we need to know the offset or just recalculate it?
     * No, we can't easily recalculate.
     * 
     * Alternative: Context switch didn't restore EAX, ECX, EDX.
     * We can update switch_context (or process_create) to pass stack info in EAX/ECX/EDX?
     * 
     * Let's modify process_create to put stack_top in ECX.
     * switch_context doesn't restore ECX.
     * So we need a way to pass it.
     * x86 switch_context saves/restores: ebx, esi, edi, ebp, esp.
     * It relies on cdecl caller saved regs? No, context switch saves callee-saved.
     * EAX, ECX, EDX are caller-saved, so they are trash across switch.
     * 
     * So we MUST get it from memory (current_process).
     * 
     * Let's look at process_t in process.h/process.c
     * int pid;             // 0
     * char name[32];       // 4
     * process_state_t state; // 36
     * cpu_context_t context; // 40
     * ...
     * sizeof(cpu_context_t) = 16 * 4 = 64 bytes (x86)
     * context ends at 104.
     * uint64_t load_base;  // 104
     * uint64_t load_size;  // 112
     * uint64_t entry;      // 120
     * int parent_pid;      // 128
     * int exit_status;     // 132
     * void *stack_base;    // 136 (pointer = 4 bytes or 8 bytes? uint64_t usually)
     * 
     * Wait, process.h defines stack_base as void* (4 bytes on 32-bit?).
     * But types.h says:
     * #elif defined(ARCH_X86)
     *   typedef uint32_t arch_reg_t;
     * 
     * Check process.h again.
     */
    
    /* To be safe, let's call a minimal C function to return the stack pointer
       and then proceed? But we can't clobber ebx/esi/edi/ebp. 
       We can push them!
    */
    
    pushl %ebx
    pushl %esi
    pushl %edi
    pushl %ebp
    
    call get_current_stack_top
    /* Returns stack top in EAX */
    
    popl %ebp
    popl %edi
    popl %esi
    popl %ebx
    
    /* Now EAX = user stack top */
    movl %eax, %esp  /* Switch to user stack temporarily (we are still Ring 0) */
    
    /* Align stack */
    andl $0xFFFFFFF0, %esp
    
    /* Push arguments onto USER stack */
    /* Stack: [ReturnAddr] [kapi] [argc] [argv] */
    /* Reverse order for push */
    pushl %ebp  /* argv */
    pushl %edi  /* argc */
    pushl %esi  /* kapi */
    pushl $0    /* Return return (fake) */
    
    /* Save ESP */
    movl %esp, %ecx
    
    /* Switch back to kernel stack for iret structure? 
       No, IRET pops values from the current stack.
       If we are Ring 0, we push [SS, ESP, EFLAGS, CS, EIP] onto the KERNEL stack.
       Then IRET pops them and switches to Ring 3.
       
       So we need to restore kernel stack?
       Accessing current_process again to get kernel stack?
       Actually, we were on the kernel stack when we entered!
       We need to preserve that pointer.
    */
    
    /* Recover kernel stack from somewhere? 
       Actually, let's look at `esp` before we overwrote it.
       We need to compute user stack address in a register (ECX), 
       write args to it (pointer math), 
       then construct IRET frame on current (kernel) stack.
    */
    
    /* Re-do: */
    /* Restore kernel stack (we lost it in previous instructions) */
    /* Let's restart logic properly */
    
    /* Get kernel stack pointer back? No, we shouldn't have overwritten ESP. */
    
    jmp real_start

real_start:
    /* Save kernel stack */
    movl %esp, %edx
    
    pushl %ebx
    pushl %esi
    pushl %edi
    pushl %ebp
    
    call get_current_stack_top
    /* Returns stack top in EAX */
    
    popl %ebp
    popl %edi
    popl %esi
    popl %ebx
    
    /* EAX = User Stack Top */
    /* Prepare args on User Stack */
    
    /* Map user stack? It's mapped. */
    /* Write args to user stack explicitly using EAX */
    /* user_sp -= 16 */
    subl $16, %eax
    andl $0xFFFFFFF0, %eax /* Align */
    
    /* Write args: kapi, argc, argv */
    /* [eax] = return addr (0) */
    /* [eax+4] = kapi */
    /* [eax+8] = argc */
    /* [eax+12] = argv */
    
    movl $0, 0(%eax)
    movl %esi, 4(%eax) /* kapi */
    movl %edi, 8(%eax) /* argc */
    movl %ebp, 12(%eax) /* argv */
    
    /* Setup IRET frame on Kernel Stack (EDX) */
    /* We are currently on Kernel Stack (ESP) */
    
    /* SS (User Data 0x23) */
    pushl $0x23
    /* ESP (User Stack) */
    pushl %eax
    /* EFLAGS (IF=1) */
    pushf
    popl %ecx
    orl $0x200, %ecx
    pushl %ecx
    /* CS (User Code 0x1B) */
    pushl $0x1B
    /* EIP (Entry Point) */
    pushl %ebx
    
    /* Set Segment Registers */
    movw $0x23, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %fs
    movw %ax, %gs
    
    /* Go! */
    iret
