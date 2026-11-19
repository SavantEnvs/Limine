/*
 * File-input target: the limine.conf parser, fed raw config text.
 * The input is the text of a limine.conf. It reaches init_config() through upstream's own raw-text
 * entry, init_config_smbios() (common/lib/config.c): Limine reads a config from an SMBIOS type 11
 * (OEM strings) structure whose string starts with "limine:config:". The harness builds that one
 * structure around the input and the shim's SMBIOS walk returns it. init_config() then strips CRs and
 * whitespace, checks the enrolled checksum slot (empty here), loads and expands ${macros} and builds
 * the menu tree; the harness walks the tree and looks up the keys the protocol loaders read, in the
 * global section and in every entry body.
 * The on-disk lookup (init_config_disk: fopen of limine.conf on FAT/ISO9660) is the `volume` target's
 * code and is checked by mayhem/test.sh; it is not repeated here, so every mutation lands on config text.
 * An SMBIOS string ends at its first NUL, so a config is the input up to its first NUL byte.
 * panic() (Limine's reaction to an unusable config) ends the process with status 1.
 */
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <lib/libc.h>
#include <lib/config.h>
#include <lib/smbios.h>
#include "shim/host.h"

extern const uint8_t *mayhem_smbios_struct;
extern size_t mayhem_smbios_struct_size;

#define MAX_INPUT (1u * 1024 * 1024)
#define PREFIX "limine:config:"

static const char *keys[] = { "timeout", "protocol", "path", "kernel_path", "module_path",
    "cmdline", "kaslr", "resolution", "wallpaper", "term_font", "interface_branding", "comment" };
static uint8_t sink;

static void query(const char *body) {
    for (size_t k = 0; k < sizeof(keys) / sizeof(keys[0]); k++) {
        for (size_t i = 0; i < 3; i++) {
            char *v = config_get_value(body, i, keys[k]);
            if (v == NULL) break;
            sink ^= (uint8_t)v[0];
        }
    }
    struct conf_tuple t = config_get_tuple(body, 0, "module_path", "module_cmdline");
    if (t.value1) sink ^= (uint8_t)t.value1[0];
}

static void walk(struct menu_entry *e, int depth) {
    for (; e != NULL && depth < 64; e = e->next) {
        if (e->name) sink ^= (uint8_t)e->name[0];
        if (e->body) query(e->body);
        walk(e->sub, depth + 1);
    }
}

int main(int argc, char **argv) {
    if (argc < 2) return 2;
    size_t len = 0;
    uint8_t *data = mayhem_read_file(argv[1], MAX_INPUT, &len);
    if (data == NULL) return 2;

    /* SMBIOS type 11: 4-byte header, string count, then the string area ending in two NULs. */
    size_t hdr_len = sizeof(struct smbios_struct_header) + 1;
    size_t plen = sizeof(PREFIX) - 1;
    size_t size = hdr_len + plen + len + 2;
    uint8_t *s = calloc(1, size);
    if (s == NULL) return 2;
    struct smbios_struct_header *hdr = (struct smbios_struct_header *)s;
    hdr->type = 11;
    hdr->length = (uint8_t)hdr_len;
    s[sizeof(struct smbios_struct_header)] = 1;
    memcpy(s + hdr_len, PREFIX, plen);
    memcpy(s + hdr_len + plen, data, len);
    mayhem_smbios_struct = s;
    mayhem_smbios_struct_size = size;

    if (!init_config_smbios()) return 0;
    query(NULL);
    walk(menu_tree, 0);
    return sink == 0x5a ? 0 : 0;
}
