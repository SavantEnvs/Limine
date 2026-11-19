/*
 * Functional probe for mayhem/test.sh, built with normal flags: runs Limine's own volume and ELF
 * parsers (the same objects the fuzz targets use) on a known image and prints what they found,
 * so test.sh can compare against known values.
 *   probe disk <img>        partitions (index first_sect sect_count) + FAT label + file contents
 *   probe elf <file>        elf_bits, entry point and load ranges
 *   probe gzip <file>       decompressed size and the first line
 *   probe image <file>      decoded width x height and the first pixel
 *   probe config <fatimg>   menu entry names and a few looked-up values
 *   probe conftext <file>   the same, for raw limine.conf text given through init_config_smbios()
 *                           (an SMBIOS OEM-strings structure, as the `config` fuzz target does)
 */
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <lib/part.h>
#include <lib/elf.h>
#include <lib/elsewhere.h>
#include <lib/libc.h>
#include <fs/file.h>
#include <mm/pmm.h>
#include <lib/config.h>
#include <lib/image.h>
#include <compress/gzip.h>
#include <lib/smbios.h>
#include "shim/host.h"

int printf(const char *fmt, ...);
int strcmp(const char *a, const char *b);
extern const uint8_t *mayhem_disk;
extern uint64_t mayhem_disk_len;
extern const uint8_t *mayhem_smbios_struct;
extern size_t mayhem_smbios_struct_size;

static void show_file(struct volume *v, const char *path) {
    struct file_handle *fh = fopen(v, path);
    if (fh == NULL) { printf("  %s: missing\n", path); return; }
    char *buf = ext_mem_alloc(fh->size + 1);
    fread(fh, buf, 0, fh->size);
    printf("  %s: %llu bytes: %s", path, (unsigned long long)fh->size, buf);
    fclose(fh);
}

int main(int argc, char **argv) {
    if (argc < 3) return 2;
    size_t len = 0;
    uint8_t *data = mayhem_read_file(argv[2], 64u << 20, &len);
    if (data == NULL) return 2;
    if (strcmp(argv[1], "gzip") == 0 || strcmp(argv[1], "image") == 0) {
        struct file_handle *fh = ext_mem_alloc(sizeof(struct file_handle));
        fh->is_memfile = true; fh->readall = true; fh->size = len;
        fh->fd = data; fh->path = strdup("/input"); fh->path_len = 7;
        if (argv[1][0] == 'g') {
            struct file_handle *gz = gzip_open(fh);
            if (gz == NULL) { printf("gzip_open failed\n"); return 0; }
            static char buf[1 << 20];
            uint64_t total = 0, n;
            while ((n = gz->read(gz, buf + total, total, sizeof(buf) - 1 - total)) > 0) total += n;
            buf[total] = 0;
            char *nl = buf; while (*nl && *nl != '\n') nl++; *nl = 0;
            printf("size %llu first-line %s\n", (unsigned long long)total, buf);
        } else {
            struct image *img = image_open(fh);
            if (img == NULL) { printf("image_open failed\n"); return 0; }
            uint32_t *px = (uint32_t *)img->img;
            size_t last = (img->img_height - 1) * (img->pitch / 4) + img->img_width - 1;
            printf("image %zux%zu bpp %d first %06x last %06x\n", img->img_width, img->img_height,
                   img->bpp, px[0] & 0xffffff, px[last] & 0xffffff);
        }
        return 0;
    }
    if (strcmp(argv[1], "config") == 0 || strcmp(argv[1], "conftext") == 0) {
        if (argv[1][4] == 't') {
            /* SMBIOS type 11: header, string count, "limine:config:<text>", two NULs. */
            size_t hl = sizeof(struct smbios_struct_header) + 1, pl = 14, size = hl + pl + len + 2;
            uint8_t *st = calloc(1, size);
            struct smbios_struct_header *h = (struct smbios_struct_header *)st;
            h->type = 11; h->length = (uint8_t)hl; st[hl - 1] = 1;
            memcpy(st + hl, "limine:config:", pl);
            memcpy(st + hl + pl, data, len);
            mayhem_smbios_struct = st; mayhem_smbios_struct_size = size;
            printf("init %d\n", init_config_smbios() ? 0 : -1);
        } else {
            mayhem_disk = data; mayhem_disk_len = len;
            static struct efi_block_io_media m; static struct efi_block_io_protocol b; b.media = &m;
            static struct volume v;
            v.block_io = &b; v.fastest_xfer_size = 64; v.sector_size = 512; v.max_partition = -1; v.sect_count = len / 512;
            printf("init %d\n", init_config_disk(&v));
        }
        char *t = config_get_value(NULL, 0, "timeout");
        printf("timeout %s\n", t ? t : "(none)");
        for (struct menu_entry *e = menu_tree; e != NULL; e = e->next) {
            char *proto = config_get_value(e->body, 0, "protocol");
            printf("entry %s protocol %s\n", e->name, proto ? proto : "(none)");
            for (struct menu_entry *s2 = e->sub; s2 != NULL; s2 = s2->next) {
                char *kp = config_get_value(s2->body, 0, "kernel_path");
                printf("  sub %s kernel_path %s\n", s2->name, kp ? kp : "(none)");
            }
        }
        return 0;
    }
    if (strcmp(argv[1], "elf") == 0) {
        uint64_t entry = 0, count = 0;
        struct elsewhere_range *r = NULL;
        int bits = elf_bits(data, len);
        printf("bits %d\n", bits);
        if (bits == 64 && elf64_load_elsewhere(data, len, 0xffffffff, &entry, &r, &count)) {
            printf("entry 0x%llx ranges %llu\n", (unsigned long long)entry, (unsigned long long)count);
            for (uint64_t i = 0; i < count; i++)
                printf("range target 0x%llx length 0x%llx\n", (unsigned long long)r[i].target,
                       (unsigned long long)r[i].length);
        }
        return 0;
    }
    mayhem_disk = data;
    mayhem_disk_len = len;
    static struct efi_block_io_media media;
    static struct efi_block_io_protocol bio;
    bio.media = &media;
    static struct volume disk;
    disk.block_io = &bio;
    disk.fastest_xfer_size = 64;
    disk.index = 1;
    disk.sector_size = 512;
    disk.max_partition = -1;
    disk.sect_count = len / 512;
    struct guid g;
    printf("valid_mbr %d gpt %d\n", is_valid_mbr(&disk), gpt_get_guid(&g, &disk));
    static struct volume parts[16];
    for (int i = 0; i < 16; i++) {
        int r = part_get(&parts[i], &disk, i);
        if (r == END_OF_TABLE || r == INVALID_TABLE) break;
        if (r != 0) continue;
        char *label = fs_get_label(&parts[i]);
        printf("part %d first %llu count %llu label %s\n", i + 1,
               (unsigned long long)parts[i].first_sect, (unsigned long long)parts[i].sect_count,
               label ? label : "(none)");
        show_file(&parts[i], "/limine.conf");
        show_file(&parts[i], "/boot/A-Long-File-Name.txt");
    }
    return 0;
}
