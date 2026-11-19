/*
 * File-input target: a boot volume image, read the way Limine reads boot media.
 *
 * The input is the whole raw disk (or optical disc, with -DMAYHEM_OPTICAL: 2048-byte sectors).
 * The harness probes it through Limine's public volume/filesystem interface:
 *   part_get() for each partition (GPT header + entries, protective MBR, MBR, EBR chains),
 *   gpt_get_guid()/mbr_get_id()/is_valid_mbr(), and on the whole disk and each partition
 *   fs_get_label() and fopen()/fread()/fclose() of the paths Limine looks for at boot
 *   (FAT12/16/32 with LFN, ISO9660 with Rock Ridge/Joliet).
 * panic() (Limine's reaction to some corrupt media) ends the process with status 1.
 */
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <lib/part.h>
#include <lib/libc.h>
#include <fs/file.h>
#include <mm/pmm.h>
#include "shim/host.h"

extern const uint8_t *mayhem_disk;
extern uint64_t mayhem_disk_len;

#ifdef MAYHEM_OPTICAL
#  define SECTOR_SIZE 2048
#  define XFER 16
#else
#  define SECTOR_SIZE 512
#  define XFER 64
#endif

#define MAX_INPUT (16u * 1024 * 1024)
#define MAX_READ  (1u * 1024 * 1024)

static const char *paths[] = {
    "/limine.conf",
    "/boot/limine/limine.conf",
    "/EFI/BOOT/BOOTX64.EFI",
    "/boot/a-rather-long-file-name.with.several.dots",
};

static uint8_t sink;

static void probe_fs(struct volume *v) {
    char *label = fs_get_label(v);
    if (label != NULL) sink ^= (uint8_t)label[0];
    struct guid g;
    if (fs_get_guid(&g, v)) sink ^= g.a;
    for (int ci = 0; ci < 2; ci++) {
        case_insensitive_fopen = ci;
        for (size_t i = 0; i < sizeof(paths) / sizeof(paths[0]); i++) {
            struct file_handle *fh = fopen(v, paths[i]);
            if (fh == NULL) continue;
            uint64_t n = fh->size < MAX_READ ? fh->size : MAX_READ;
            uint8_t *buf = ext_mem_alloc(n ? n : 1);
            fread(fh, buf, 0, n);
            if (n) sink ^= buf[n - 1];
            if (n > 1) {   /* a second, unaligned read inside the file */
                fread(fh, buf, n / 2, n - n / 2);
                sink ^= buf[0];
            }
            pmm_free(buf, n);
            fclose(fh);
        }
    }
}

int main(int argc, char **argv) {
    if (argc < 2) return 2;
    size_t len = 0;
    uint8_t *data = mayhem_read_file(argv[1], MAX_INPUT, &len);
    if (data == NULL) return 2;
    mayhem_disk = data;
    mayhem_disk_len = len;

    static struct efi_block_io_media media;
    static struct efi_block_io_protocol bio;
    bio.media = &media;
    media.block_size = SECTOR_SIZE;
    media.last_block = len / SECTOR_SIZE ? len / SECTOR_SIZE - 1 : 0;

    static struct volume disk;
    disk.block_io = &bio;
    disk.fastest_xfer_size = XFER;
    disk.index = 1;
#ifdef MAYHEM_OPTICAL
    disk.is_optical = true;
#endif
    disk.partition = 0;
    disk.sector_size = SECTOR_SIZE;
    disk.max_partition = -1;
    disk.first_sect = 0;
    disk.sect_count = len / 512;

    sink ^= (uint8_t)is_valid_mbr(&disk);
    sink ^= (uint8_t)mbr_get_id(&disk);
    struct guid g;
    if (gpt_get_guid(&g, &disk)) sink ^= g.a;

    probe_fs(&disk);

    static struct volume parts[32];
    for (int i = 0; i < 32; i++) {
        int r = part_get(&parts[i], &disk, i);
        if (r == END_OF_TABLE || r == INVALID_TABLE) break;
        if (r != 0) continue;
        probe_fs(&parts[i]);
    }
    return sink == 0x5a ? 0 : 0;
}
