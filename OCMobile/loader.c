#include <stdint.h>
#include <stddef.h>

/*
 * OpenCore Mobile – Prototype Loader
 * Stage 0: Control + Visibility
 */

#define OCM_BOOT_MAGIC 0x4F434D424F4F54ULL /* "OCMBOOT" */

/* ---- boot parameters (opaque for now) ---- */
struct ocm_boot_params {
    void *opaque;   /* future: dtb, framebuffer, memory map */
};

/* ---- platform hooks (to be implemented per device) ---- */
static void ocm_console_putc(char c);
static void ocm_halt(void);

/* ---- panic: loud, final, honest ---- */
static void ocm_panic(const char *msg) {
    while (*msg) {
        ocm_console_putc(*msg++);
    }
    ocm_console_putc('\n');
    ocm_halt();
}

/* ---- entry point ---- */
void boot_main(uint64_t magic, void *params) {
    (void)params; /* unused in prototype stage */

    if (magic != OCM_BOOT_MAGIC) {
        /* silent refusal: caller is not trusted */
        ocm_halt();
    }

    /* visible proof of life */
    ocm_console_putc('O');
    ocm_console_putc('C');
    ocm_console_putc('M');
    ocm_console_putc('\n');

    /* explicit stop: nothing else exists yet */
    ocm_panic("OCM: prototype loader reached");
//    boot_menu_summon();
}

/* =========================================================
 * Platform-specific minimal implementations
 * =========================================================
 * Replace these with UART / framebuffer writes per device.
 */

#define UART_BASE 0x09000000 /* example address */

static void ocm_console_putc(char c) {
    volatile uint32_t *uart = (volatile uint32_t *)UART_BASE;
    *uart = (uint32_t)c;
}

static void ocm_halt(void) {
    for (;;) {
        __asm__ volatile ("wfi");
    }
}
