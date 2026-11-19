/* Shared by the host-CLI harnesses: host/limine.c is included into the harness TU with its main()
 * renamed, so the harness can reset the tool's file-scope state before each input. The CLI opens
 * its target "r+b" and patches it in place, so each input is written to a fresh scratch file
 * under $TMPDIR first. */
#define main limine_main
#include "limine.c"
#undef main
#include <unistd.h>

static int write_scratch(const uint8_t *data, size_t size, char *path, size_t pathlen) {
    const char *tmp = getenv("TMPDIR");
    if (tmp == NULL || *tmp == '\0') tmp = "/tmp";
    snprintf(path, pathlen, "%s/limine-fuzz-XXXXXX", tmp);
    int fd = mkstemp(path);
    if (fd < 0) return -1;
    size_t off = 0;
    while (off < size) {
        ssize_t w = write(fd, data + off, size - off);
        if (w <= 0) { close(fd); unlink(path); return -1; }
        off += (size_t)w;
    }
    close(fd);
    return 0;
}
