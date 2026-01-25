/*
 * UnixOS - C Runtime Startup Code (crt0)
 * 
 * Entry point for userspace programs.
 */

.global _start
.type _start, %function

.extern main
.extern exit
.extern __libc_init

_start:
    /* Clear frame pointer for stack traces */
    mov     x29, #0
    mov     x30, #0
    
    /* argc is in x0, argv in x1, envp in x2 (from kernel) */
    /* These are passed on stack for most ABIs, adjust as needed */
    
    /* For now, use simple argc=0, argv=NULL, envp=NULL */
    mov     x0, #0          /* argc */
    mov     x1, #0          /* argv */
    mov     x2, #0          /* envp */
    
    /* Initialize libc */
    /* bl    __libc_init */
    
    /* Call main(argc, argv, envp) */
    bl      main
    
    /* Exit with return value from main */
    bl      exit
    
    /* Should not reach here, but just in case */
1:
    wfi
    b       1b

.size _start, . - _start
