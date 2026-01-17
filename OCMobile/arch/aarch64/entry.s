.global _start
.extern boot_main

_start:
    /* 1. Setup Stack (Critical for C execution) */
    /* Must be 16-byte aligned for AArch64 hardware */
    ldr x0, =stack_top
    mov sp, x0

    /* 2. Prepare arguments for: void boot_main(uint64_t magic, void *boot_args) */
    mov x0, #0xFEEDFACE      /* Param 1: Magic value for XNU compatibility */
    ldr x1, =boot_params     /* Param 2: Pointer to a struct/data in memory */

    /* 3. Call the C function */
    bl boot_main

    /* 4. If C returns, halt */
hang:
    wfi
    b hang

.section .bss
.align 16
stack_base: .skip 0x4000     /* 16KB stack */
stack_top:
