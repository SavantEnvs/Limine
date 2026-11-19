/*
 * File-input target over the decoders Limine applies to files it loads from boot media, fed the
 * input as an in-memory file handle (the "memfile" handles Limine itself builds):
 *   -DMAYHEM_GZIP:  gzip_open() (the `$` URI prefix: gzip container + inflate) and a full
 *                   sequential read, then a random-access read back near the start;
 *   -DMAYHEM_IMAGE: image_open() (wallpapers: BMP/PNG/JPEG/... via stb_image, and QOI), then
 *                   image_make_stretched()/image_make_centered() and image_close().
 * panic() (Limine's reaction to a corrupt stream) ends the process with status 1.
 */
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <lib/libc.h>
#include <fs/file.h>
#include <mm/pmm.h>
#include <compress/gzip.h>
#include <lib/image.h>
#include "shim/host.h"

#define MAX_INPUT (16u * 1024 * 1024)
#define MAX_OUTPUT (64u * 1024 * 1024)

int main(int argc, char **argv) {
    if (argc < 2) return 2;
    size_t len = 0;
    uint8_t *data = mayhem_read_file(argv[1], MAX_INPUT, &len);
    if (data == NULL) return 2;

    struct file_handle *fh = ext_mem_alloc(sizeof(struct file_handle));
    fh->is_memfile = true;
    fh->readall = true;
    fh->size = len;
    fh->fd = ext_mem_alloc(len ? len : 1);
    memcpy(fh->fd, data, len);
    fh->path = strdup("/input");
    fh->path_len = 7;

#if defined(MAYHEM_GZIP)
    struct file_handle *gz = gzip_open(fh);
    if (gz == NULL) return 0;
    static uint8_t buf[65536];
    uint64_t total = 0, n;
    while (total < MAX_OUTPUT && (n = gz->read(gz, buf, total, sizeof(buf))) > 0)
        total += n;
    if (total > 16)
        gz->read(gz, buf, 7, 9);
    fclose(gz);
#elif defined(MAYHEM_IMAGE)
    struct image *img = image_open(fh);
    if (img == NULL) return 0;
    image_make_stretched(img, 64, 48);
    image_make_centered(img, 640, 480, 0x123456);
    image_close(img);
#else
#  error "define MAYHEM_GZIP or MAYHEM_IMAGE"
#endif
    return 0;
}
