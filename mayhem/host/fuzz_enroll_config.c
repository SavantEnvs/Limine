/*
 * libFuzzer target: `limine enroll-config <bootloader> <blake2b hash>` (and `--reset`) from
 * host/limine.c, on a bootloader file (BOOT*.EFI / limine-bios.sys): argument handling, the
 * BLAKE2B length/hex validation, the search for the config-checksum signature and the in-place
 * patch.
 * Input layout:
 *   byte 0      options: bit0 --reset, bit1 hash comes from the input, bit2 add a surplus
 *               positional argument, bit3 omit the bootloader path, bit4 drop --quiet
 *   if bit1:    byte 1 = hash length N, then N bytes of hash (cut at the first NUL)
 *   the rest    the bootloader file
 */
#include "fuzz_common.h"

static char fixed_hash[] =
    "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"
    "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size < 1) return 0;
    uint8_t opts = data[0];
    data++; size--;

    char hash[256];
    char *hash_arg = fixed_hash;
    if (opts & 2) {
        if (size < 1) return 0;
        size_t n = data[0];
        data++; size--;
        if (n > size) n = size;
        memcpy(hash, data, n);
        hash[n] = '\0';
        data += n; size -= n;
        hash_arg = hash;
    }

    char path[4096];
    if (write_scratch(data, size, path, sizeof(path)) != 0) return 0;
    quiet = false;
    char *argv[8];
    int argc = 0;
    argv[argc++] = "limine";
    argv[argc++] = "enroll-config";
    if (!(opts & 16)) argv[argc++] = "--quiet";
    if (!(opts & 8)) argv[argc++] = path;
    if (opts & 1) argv[argc++] = "--reset";
    else argv[argc++] = hash_arg;
    if (opts & 4) argv[argc++] = "extra";
    argv[argc] = NULL;
    limine_main(argc, argv);
    unlink(path);
    return 0;
}
