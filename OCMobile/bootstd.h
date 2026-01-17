#ifndef BOOTLIB_H
#define BOOTLIB_H

#include <stdint.h>
#include <stddef.h>

/* =========================
 *  Basic types
 * ========================= */

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t   s8;
typedef int16_t  s16;
typedef int32_t  s32;
typedef int64_t  s64;

/* =========================
 *  Console / Text Output
 * ========================= */

/* Initialize text output (UART or framebuffer text mode) */
void console_init(void);

/* Output a single character */
void putc(char c);

/* Output a null-terminated string */
void puts(const char *s);

/* Minimal printf (supports %s %c %x %d %u only) */
void printf(const char *fmt, ...);

/* Clear screen (if framebuffer-backed) */
void console_clear(void);

/* =========================
 *  Input (early boot)
 * ========================= */

typedef enum {
    KEY_NONE = 0,
    KEY_UP,
    KEY_DOWN,
    KEY_LEFT,
    KEY_RIGHT,
    KEY_SELECT,
    KEY_BACK
} boot_key_t;

/* Initialize input subsystem (GPIO keys, volume buttons, etc.) */
void input_init(void);

/* Poll input (non-blocking) */
boot_key_t input_poll(void);

/* Blocking wait for a key */
boot_key_t input_get(void);

/* =========================
 *  Framebuffer (optional)
 * ========================= */

typedef struct {
    void   *base;
    u32     width;
    u32     height;
    u32     pitch;
    u32     bpp;
} framebuffer_t;

/* Initialize framebuffer if available */
int framebuffer_init(framebuffer_t *fb);

/* Draw a filled rectangle */
void fb_fill_rect(
    framebuffer_t *fb,
    u32 x, u32 y,
    u32 w, u32 h,
    u32 color
);

/* =========================
 *  Storage / Filesystem
 * ========================= */

typedef struct {
    void *impl;   /* opaque FS handle */
} fs_t;

typedef struct {
    void *impl;   /* opaque file handle */
} file_t;

/* Mount filesystem (e.g., SD card root) */
int fs_mount(fs_t *fs, const char *path);

/* Open file */
int fs_open(fs_t *fs, const char *path, file_t *out);

/* Read from file */
ssize_t fs_read(file_t *file, void *buf, size_t size);

/* Close file */
void fs_close(file_t *file);

/* =========================
 *  Memory
 * ========================= */

/* Early boot allocator (bump allocator) */
void *boot_alloc(size_t size);

/* Memory set / copy (no libc) */
void *memcpy(void *dst, const void *src, size_t n);
void *memset(void *dst, int val, size_t n);
int   memcmp(const void *a, const void *b, size_t n);

/* =========================
 *  Time / Delay
 * ========================= */

/* Busy-wait delay (microseconds if possible) */
void udelay(u32 usec);

/* =========================
 *  Panic / Halt
 * ========================= */

__attribute__((noreturn))
void panic(const char *reason);

/* Halt CPU forever */
__attribute__((noreturn))
void halt(void);

#endif /* BOOTLIB_H */
