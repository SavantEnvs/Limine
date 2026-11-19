/*
 * File-input target: an ELF kernel image, as Limine's protocol loaders read one.
 * elf_bits() picks the class; then the section header lookup, the ".limine_reqs" section load
 * (with relocation, as protos/limine.c does), and the "load elsewhere" program-header walk the
 * multiboot loaders use (ranges recorded against the in-memory file, then staged).
 * panic() (Limine's rejection of a malformed kernel) ends the process with status 1.
 */
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <lib/elf.h>
#include <lib/elsewhere.h>
#include <lib/libc.h>
#include <mm/pmm.h>
#include "shim/host.h"

#define MAX_INPUT (16u * 1024 * 1024)

int main(int argc, char **argv) {
    if (argc < 2) return 2;
    size_t len = 0;
    uint8_t *elf = mayhem_read_file(argv[1], MAX_INPUT, &len);
    if (elf == NULL) return 2;

    int bits = elf_bits(elf, len);
    uint64_t entry = 0, count = 0;
    struct elsewhere_range *ranges = NULL;
    static uint8_t section[65536];

    if (bits == 64) {
        struct elf_section_hdr_info info = elf64_section_hdr_info(elf, len);
        (void)info;
        elf64_load_section(elf, len, section, ".limine_reqs", sizeof(section), 0);
        elf64_load_section(elf, len, section, ".limine_reqs", sizeof(section), 0x200000);
        if (elf64_load_elsewhere(elf, len, 0xffffffff, &entry, &ranges, &count))
            elf_stage_elsewhere(elf, ranges, count);
    } else if (bits == 32) {
        struct elf_section_hdr_info info = elf32_section_hdr_info(elf, len);
        (void)info;
        if (elf32_load_elsewhere(elf, len, 0xffffffff, &entry, &ranges, &count))
            elf_stage_elsewhere(elf, ranges, count);
    }
    return (int)(entry & 0);
}
