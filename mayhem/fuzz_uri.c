/*
 * File-input target: a Limine URI (as written in limine.conf PATH/MODULE_PATH/KERNEL_PATH...)
 * opened against a boot disk. Input = the URI text, a newline, then the raw disk image.
 * uri_open() parses resource(root):/path#hash, dispatches boot()/hdd()/odd()/guid()/uuid()/
 * fslabel() through the volume index (built from the disk with part_get(), as disk discovery
 * does), verifies a blake2b hash when one is given, gunzips a `$`-prefixed resource, and returns
 * the file, which is then read in full.
 * panic() ends the process with status 1.
 */
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <lib/part.h>
#include <lib/libc.h>
#include <lib/misc.h>
#include <lib/uri.h>
#include <fs/file.h>
#include <mm/pmm.h>
#include "shim/host.h"

extern const uint8_t *mayhem_disk;
extern uint64_t mayhem_disk_len;

#define MAX_INPUT (16u * 1024 * 1024)
#define MAX_URI 4096

int main(int argc, char **argv) {
    if (argc < 2) return 2;
    size_t len = 0;
    uint8_t *data = mayhem_read_file(argv[1], MAX_INPUT, &len);
    if (data == NULL) return 2;
    size_t ulen = 0;
    while (ulen < len && ulen < MAX_URI && data[ulen] != '\n') ulen++;
    if (ulen >= len || data[ulen] != '\n') return 0;   /* no URI line */
    char *uri = ext_mem_alloc(ulen + 1);
    memcpy(uri, data, ulen);
    mayhem_disk = data + ulen + 1;
    mayhem_disk_len = len - ulen - 1;

    static struct efi_block_io_media media;
    static struct efi_block_io_protocol bio;
    bio.media = &media;
    static struct volume disk;
    disk.block_io = &bio;
    disk.fastest_xfer_size = 64;
    disk.index = 1;
    disk.partition = 0;
    disk.sector_size = 512;
    disk.max_partition = -1;
    disk.sect_count = mayhem_disk_len / 512;

    static struct volume *index[34];
    static struct volume parts[32];
    volume_index = index;
    volume_index_i = 0;
    disk.guid_valid = gpt_get_guid(&disk.guid, &disk);
    index[volume_index_i++] = &disk;
    for (int i = 0; i < 32; i++) {
        int r = part_get(&parts[i], &disk, i);
        if (r == END_OF_TABLE || r == INVALID_TABLE) break;
        if (r != 0) continue;
        parts[i].fslabel = fs_get_label(&parts[i]);
        parts[i].fslabel_valid = parts[i].fslabel != NULL;
        parts[i].part_guid_valid = parts[i].part_guid_valid || fs_get_guid(&parts[i].part_guid, &parts[i]);
        if (parts[i].partition > disk.max_partition) disk.max_partition = parts[i].partition;
        index[volume_index_i++] = &parts[i];
    }
    boot_volume = volume_index_i > 1 ? index[1] : &disk;

    struct file_handle *fh = uri_open(uri, MEMMAP_BOOTLOADER_RECLAIMABLE, false);
    if (fh == NULL) return 0;
    uint8_t sink = 0;
    for (uint64_t i = 0; i < fh->size; i += 4096) sink ^= ((uint8_t *)fh->fd)[i];
    fclose(fh);
    return sink == 0x5a ? 0 : 0;
}
