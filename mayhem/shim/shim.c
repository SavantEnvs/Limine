/*
 * Host stand-ins for the firmware-facing pieces of Limine's common/ code, so its parsers
 * (partition tables, FAT, ISO9660, ELF, ...) run as an ordinary Linux process:
 *   - the physical memory manager (ext_mem_alloc*, pmm_free) becomes calloc/free, refusing what
 *     a real machine could not hold (Limine panics on a failed allocation too);
 *   - panic() prints its format string and ends the process with status 1, as the bootloader
 *     stops on it; the targets are file-input (one input per process), so nothing is unwound;
 *   - the disk driver (disk_read_sectors) reads from the input image held by the harness.
 * Compiled with the same -include rename.h as the Limine TUs.
 */
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdarg.h>
#include <lib/misc.h>
#include <lib/print.h>
#include <lib/libc.h>
#include <lib/part.h>
#include <mm/pmm.h>
#include <fs/file.h>
#include <drivers/disk.h>
#include <pxe/tftp.h>
#include <lib/smbios.h>
#include <lib/term.h>
#include <lib/getchar.h>
#include <menu.h>
#include "host.h"

#undef strdup
extern char *strdup(const char *s);

/* verbose/quiet/terse/... are defined by common/lib/misc.s2.c (all false). */

#define MAYHEM_ALLOC_MAX (256u * 1024 * 1024)

void *ext_mem_alloc_type_aligned_mode(uint64_t count, uint32_t type, size_t alignment, bool allow_high) {
    (void)type; (void)alignment; (void)allow_high;
    if (count > MAYHEM_ALLOC_MAX)
        panic(false, "ext_mem_alloc: out of memory");
    void *p = calloc(1, count ? count : 1);
    if (p == NULL)
        panic(false, "ext_mem_alloc: out of memory");
    return p;
}
void *ext_mem_alloc_type_aligned(uint64_t count, uint32_t type, size_t alignment) {
    return ext_mem_alloc_type_aligned_mode(count, type, alignment, false);
}
void *ext_mem_alloc_type(uint64_t count, uint32_t type) {
    return ext_mem_alloc_type_aligned(count, type, 4096);
}
void *ext_mem_alloc(uint64_t count) { return ext_mem_alloc_type(count, MEMMAP_BOOTLOADER_RECLAIMABLE); }
void *ext_mem_alloc_size_t(size_t count) { return ext_mem_alloc(count); }
void *ext_mem_alloc_nopanic(uint64_t count) {
    if (count > MAYHEM_ALLOC_MAX) return NULL;
    return calloc(1, count ? count : 1);
}
void *ext_mem_alloc_counted(uint64_t count, uint64_t elem_size) {
    if (elem_size != 0 && count > UINT64_MAX / elem_size)
        panic(false, "ext_mem_alloc_counted: overflow");
    return ext_mem_alloc(count * elem_size);
}
void *ext_mem_alloc_type_aligned_below(uint64_t count, uint32_t type, size_t alignment, uint64_t limit, bool bottom_up) {
    (void)limit; (void)bottom_up;
    return ext_mem_alloc_type_aligned(count, type, alignment);
}
void *conv_mem_alloc(uint64_t count) { return ext_mem_alloc(count); }
void pmm_free(void *ptr, uint64_t length) { (void)length; free(ptr); }
void pmm_free_size_t(void *ptr, size_t length) { pmm_free(ptr, length); }

static size_t slen(const char *s) { size_t n = 0; while (s[n]) n++; return n; }

void vprint(const char *fmt, va_list args) { (void)fmt; (void)args; }
void print(const char *fmt, ...) { (void)fmt; }

noreturn void panic(bool allow_menu, const char *fmt, ...) {
    (void)allow_menu;
    write(2, "PANIC: ", 7);
    write(2, fmt, slen(fmt));
    write(2, "\n", 1);
    _exit(1);
}

char *lm_strdup(const char *s) {
    char *r = ext_mem_alloc(slen(s) + 1);
    memcpy(r, s, slen(s) + 1);
    return r;
}

struct file_handle *tftp_open(struct volume *part, const char *server_addr, const char *name) {
    (void)part; (void)server_addr; (void)name;
    return NULL;
}

/* The disk: the harness points these at the input image. */
const uint8_t *mayhem_disk = NULL;
uint64_t mayhem_disk_len = 0;

int disk_read_sectors(struct volume *volume, void *buf, uint64_t block, size_t count) {
    uint64_t ss = (uint64_t)volume->sector_size;
    if (ss == 0 || block > UINT64_MAX / ss || count > UINT64_MAX / ss)
        return DISK_FAILURE;
    uint64_t off = block * ss, len = (uint64_t)count * ss;
    if (off > mayhem_disk_len || len > mayhem_disk_len - off)
        return DISK_FAILURE;
    memcpy(buf, mayhem_disk + off, len);
    return DISK_SUCCESS;
}

uint8_t *mayhem_read_file(const char *path, size_t max, size_t *len) {
    int fd = open(path, 0);
    if (fd < 0) return NULL;
    uint8_t *buf = malloc(max ? max : 1);
    size_t n = 0;
    long r;
    while (n < max && (r = read(fd, buf + n, max - n)) > 0) n += (size_t)r;
    close(fd);
    *len = n;
    return buf;
}

/* KASLR randomness: fixed, so a given input always takes the same path. */
uint32_t safe_rand32(void) { return 0x12345000u; }
uint64_t safe_rand64(void) { return 0x123456789abcd000ull; }

/* Globals common/lib/misc.c would define (it is firmware glue, not linked). */
bool editor_enabled = true;
bool help_hidden = false;
bool secure_boot_active = false;
int lm_abs(int i) { return i < 0 ? -i : i; }

/* UEFI boot services: config.c asks for the loaded-image protocol to find the directory it was
 * booted from; there is none, so it falls back to the default search path, as on firmware that
 * fails the call. */
static efi_status EFI_API mayhem_handle_protocol(efi_handle h, struct efi_guid *g, void **out) {
    (void)h; (void)g;
    *out = NULL;
    return 0x800000000000000eULL; /* EFI_NOT_FOUND */
}
static struct efi_boot_services mayhem_bs = { .handle_protocol = mayhem_handle_protocol };
struct efi_boot_services *gBS = &mayhem_bs;
efi_handle efi_image_handle = NULL;

/* SMBIOS: no firmware tables on the host. A harness may set mayhem_smbios_struct to one structure
 * (header + strings, mayhem_smbios_struct_size bytes); the walk then yields it once, the way
 * smbios_walk_next() hands init_config_smbios() each structure of a real table. Unset: no table. */
const uint8_t *mayhem_smbios_struct = NULL;
size_t mayhem_smbios_struct_size = 0;
bool smbios_walk_start(struct smbios_walk *walk) {
    walk->table = (uintptr_t)mayhem_smbios_struct;
    walk->length = mayhem_smbios_struct_size;
    walk->offset = 0;
    walk->count = 1;
    walk->index = 0;
    return mayhem_smbios_struct != NULL;
}
struct smbios_struct_header *smbios_walk_next(struct smbios_walk *walk, size_t *size) {
    if (walk->table == 0 || walk->index >= walk->count) return NULL;
    walk->index++;
    *size = walk->length;
    return (struct smbios_struct_header *)walk->table;
}

/* For uri.c. The boot volume is set by the harness; there is no other disk to connect. */
struct volume *boot_volume = NULL;
bool disk_connect_all(void) { return false; }
/* memmap bookkeeping of host allocations: nothing to record. */
bool memmap_alloc_range(uint64_t base, uint64_t length, uint32_t type, uint32_t overlay_type,
                        bool do_panic, bool simulation, bool new_entry) {
    (void)base; (void)length; (void)type; (void)overlay_type; (void)do_panic; (void)simulation; (void)new_entry;
    return true;
}
void term_notready(void) {}
/* The hash-mismatch prompt: answer "no", which returns to the menu; the menu ends the run. */
int lm_getchar(void) { return 'n'; }
noreturn void menu(bool first_run) { (void)first_run; _exit(1); }
/* TFTP is out of scope (tftp_open above finds nothing): every server address is refused. */
int lm_inet_pton(const char *src, void *dst) { (void)src; (void)dst; return -1; }
