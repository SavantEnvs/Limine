/*
 * libFuzzer target: `limine bios-install <image> [partition]` from host/limine.c, on a disk image.
 * Reaches the host tool's MBR/GPT parsing (header + entry-array CRCs, backup GPT location),
 * GPT-to-MBR isohybrid conversion, stage 2 placement and the undo path.
 * Input byte 0 picks the command-line options (a harness parameter, not part of the image):
 *   bit0 --force, bit1 GPT partition index "1", bit2 index "2",
 *   bit3 --no-gpt-to-mbr-isohybrid-conversion. The rest is the disk image.
 * File-scope state of the tool is reset before each input; the image is a fresh scratch file.
 */
#include "fuzz_common.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size < 1) return 0;
    uint8_t opts = data[0];
    char path[4096];
    if (write_scratch(data + 1, size - 1, path, sizeof(path)) != 0) return 0;

    quiet = false;
    cache_state = 0; cached_block = 0; cache = NULL; device = NULL; block_size = 0;
    uninstalling = false; uninstall_data_i = 0; uninstall_file = NULL;

    char *argv[8];
    int argc = 0;
    argv[argc++] = "limine";
    argv[argc++] = "bios-install";
    argv[argc++] = "--quiet";
    if (opts & 1) argv[argc++] = "--force";
    if (opts & 8) argv[argc++] = "--no-gpt-to-mbr-isohybrid-conversion";
    argv[argc++] = path;
    if (opts & 2) argv[argc++] = "1";
    else if (opts & 4) argv[argc++] = "2";
    argv[argc] = NULL;
    limine_main(argc, argv);
    device = NULL;
    cache = NULL;
    unlink(path);
    return 0;
}
