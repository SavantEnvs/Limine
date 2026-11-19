#!/usr/bin/env bash
#
# mayhem/build.sh — build Limine's fuzz targets and the functional suite.
#
# Limine is a freestanding bootloader; its parsers live in common/. They are compiled here for the
# Linux host as the x86_64 UEFI configuration (-DUEFI, freestanding headers, PicoEFI types), with
# mayhem/shim/ standing in for the firmware (memory manager, disk driver, panic). The vendored
# dependencies upstream's ./bootstrap would clone are pre-fetched by the Dockerfile at the commits
# bootstrap pins, under /opt/toolchains/deps (SPEC 6.2 item 19), so this script runs offline.
#   out/volume, out/iso        AFL file-input targets (Limine panics = exits on corrupt media)
#   out/elf, out/gzip, out/image, out/config   AFL file-input targets: ELF loader, gzip
#                              layer, wallpaper decoders, limine.conf parser
#   out/uri                    AFL file-input target: URI string + boot disk -> uri_open()
#   out/fuzz_bios_install, out/fuzz_enroll_config   libFuzzer targets over the host `limine` CLI
#   out/test/*                 normal-flags builds used by mayhem/test.sh
set -euo pipefail
[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH

: "${SANITIZER_FLAGS=-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer}"
: "${DEBUG_FLAGS:=-g -gdwarf-3}"
: "${CC:=clang}" ; : "${CXX:=clang++}" ; : "${LIB_FUZZING_ENGINE:=-fsanitize=fuzzer}"
: "${STANDALONE_FUZZ_MAIN:=/opt/mayhem/StandaloneFuzzTargetMain.c}"
: "${MAYHEM_JOBS:=$(nproc)}"
: "${COVERAGE_FLAGS=}"
: "${SRC:=/mayhem}"
: "${LIMINE_DEPS:=/opt/toolchains/deps}"
export SANITIZER_FLAGS DEBUG_FLAGS CC CXX LIB_FUZZING_ENGINE MAYHEM_JOBS

cd "$SRC"
OUT="$SRC/mayhem/out"
mkdir -p "$OUT/obj-afl" "$OUT/obj-test" "$OUT/test"

FCH="$LIMINE_DEPS/freestanding-c-hdrs-59d91c6/src/include"
PICO="$LIMINE_DEPS/picoefi-9065da0/src/include"
PROTO="$LIMINE_DEPS/limine-protocol-877c2d4/src/include"
FLANTERM="$LIMINE_DEPS/flanterm-3e311fe/src/src"
LIBFDT="$LIMINE_DEPS/libfdt-7bf94e6/src/src"
PDGZIP="$LIMINE_DEPS/pdgzip-0101976/src"
STBI="$LIMINE_DEPS/stbi-hardened-b85b7e8/src"
for d in "$FCH" "$PICO" "$PROTO" "$FLANTERM" "$LIBFDT" "$PDGZIP" "$STBI"; do
    [ -d "$d" ] || { echo "missing dependency dir $d (built by mayhem/Dockerfile)" >&2; exit 1; }
done

# What ./bootstrap would copy into common/: pdgzip (as is) and stb_image.h with upstream's
# common/stb_image.patch applied. Generated under mayhem/out/gen (gitignored), searched first.
GEN="$OUT/gen"
rm -rf "$GEN" && mkdir -p "$GEN/lib" "$GEN/compress"
cp "$PDGZIP/pdgzip.c" "$PDGZIP/pdgzip.h" "$GEN/compress/"
cp "$STBI/include/stb_image.h" "$GEN/lib/stb_image.h"
patch -s "$GEN/lib/stb_image.h" < "$SRC/common/stb_image.patch"
rm -f "$GEN/lib/stb_image.h.orig"

LIMINE_CFLAGS=(-std=gnu11 -ffreestanding -fno-builtin -fno-stack-protector -O1
    -DUEFI -DCOM_OUTPUT=false -DE9_OUTPUT=false -DFLANTERM_IN_FLANTERM
    -include "$SRC/mayhem/shim/rename.h"
    -I "$GEN" -I "$SRC/common" -I "$SRC/mayhem/shim" -I "$SRC/mayhem" -I "$SRC/common/libc-compat"
    -isystem "$FCH" -I "$PICO" -I "$FLANTERM" -I "$LIBFDT" -I "$PROTO"
    -Wno-unused-command-line-argument)

LIMINE_SRCS=(common/lib/part.s2.c common/fs/file.s2.c common/fs/fat32.s2.c common/fs/iso9660.s2.c
    common/lib/utf.s2.c common/lib/cp437.s2.c common/crypt/blake2b.c common/lib/guid.c
    common/lib/elf.c common/lib/misc.s2.c common/compress/gzip.c mayhem/out/gen/compress/pdgzip.c
    common/lib/image.c common/lib/stb_image.c common/lib/qoi.c common/lib/config.c
    common/lib/uri.c mayhem/shim/shim.c)

# compile <cc> <objdir> <extra flags...> -- <sources...>
compile() {
    local cc="$1" objdir="$2"; shift 2
    local flags=()
    while [ "$1" != "--" ]; do flags+=("$1"); shift; done; shift
    for s in "$@"; do
        local o="$objdir/$(echo "$s" | tr / _).o"
        "$cc" "${LIMINE_CFLAGS[@]}" "${flags[@]}" -c "$s" -o "$o"
    done
}
objs() { local objdir="$1"; shift; for s in "$@"; do echo "$objdir/$(echo "$s" | tr / _).o"; done; }

# 1) AFL file-input targets: the Limine parsers + shim, instrumented, ASan+UBSan.
export AFL_QUIET=1
read -r -a SAN <<<"$SANITIZER_FLAGS"
read -r -a DBG <<<"$DEBUG_FLAGS"
compile afl-clang-fast "$OUT/obj-afl" "${SAN[@]}" "${DBG[@]}" -- "${LIMINE_SRCS[@]}"
afl-clang-fast "${SAN[@]}" "${DBG[@]}" -c mayhem/lsan_off.c -o "$OUT/obj-afl/lsan_off.o"
mapfile -t AFL_OBJS < <(objs "$OUT/obj-afl" "${LIMINE_SRCS[@]}")
for t in volume iso elf gzip image config uri; do
    extra=()
    src=mayhem/fuzz_volume.c
    case "$t" in
        iso) extra=(-DMAYHEM_OPTICAL) ;;
        elf) src=mayhem/fuzz_elf.c ;;
        gzip) src=mayhem/fuzz_file.c; extra=(-DMAYHEM_GZIP) ;;
        image) src=mayhem/fuzz_file.c; extra=(-DMAYHEM_IMAGE) ;;
        config) src=mayhem/fuzz_config.c ;;
        uri) src=mayhem/fuzz_uri.c ;;
    esac
    afl-clang-fast "${LIMINE_CFLAGS[@]}" "${SAN[@]}" "${DBG[@]}" "${extra[@]}" -c "$src" -o "$OUT/obj-afl/harness_$t.o"
    afl-clang-fast "${SAN[@]}" "${DBG[@]}" "$OUT/obj-afl/harness_$t.o" "${AFL_OBJS[@]}" \
        "$OUT/obj-afl/lsan_off.o" -o "$OUT/$t"
done

# 2) libFuzzer targets over host/limine.c (the `limine` CLI), plus standalone reproducers.
HOSTFLAGS=(-std=gnu99 -D_FILE_OFFSET_BITS=64 -I "$SRC/mayhem/host" -I "$SRC/host" -O1)
$CC "${SAN[@]}" "${DBG[@]}" -c mayhem/lsan_off.c -o "$OUT/lsan_off.o"
$CC "${SAN[@]}" "${DBG[@]}" -c "$STANDALONE_FUZZ_MAIN" -o "$OUT/standalone_main.o"
for t in bios_install enroll_config; do
    $CC "${HOSTFLAGS[@]}" "${SAN[@]}" "${DBG[@]}" -fsanitize=fuzzer-no-link -c "mayhem/host/fuzz_$t.c" -o "$OUT/fuzz_$t.o"
    $CC "${SAN[@]}" "${DBG[@]}" $LIB_FUZZING_ENGINE "$OUT/fuzz_$t.o" "$OUT/lsan_off.o" -o "$OUT/fuzz_$t"
    $CC "${SAN[@]}" "${DBG[@]}" "$OUT/fuzz_$t.o" "$OUT/standalone_main.o" "$OUT/lsan_off.o" -o "$OUT/fuzz_$t-standalone"
done

# 3) Functional suite, normal flags: the real `limine` host tool and the parser probe.
read -r -a COV <<<"$COVERAGE_FLAGS"
$CC -std=c99 -D_FILE_OFFSET_BITS=64 -O2 -I "$SRC/mayhem/host" "${COV[@]}" host/limine.c -o "$OUT/test/limine"
compile "$CC" "$OUT/obj-test" "${COV[@]}" -- "${LIMINE_SRCS[@]}" mayhem/test/probe.c
mapfile -t TEST_OBJS < <(objs "$OUT/obj-test" "${LIMINE_SRCS[@]}" mayhem/test/probe.c)
$CC "${COV[@]}" "${TEST_OBJS[@]}" -o "$OUT/test/probe"
echo "build ok"
