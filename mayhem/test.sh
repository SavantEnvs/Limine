#!/usr/bin/env bash
#
# mayhem/test.sh — known-answer tests for Limine, run on what mayhem/build.sh built with normal flags:
#   - out/test/probe: Limine's own partition/FAT/ELF parsers (common/) on committed images, output
#     compared with the known partition geometry, volume labels, file contents and ELF entry point;
#   - out/test/limine: the real host `limine` tool, `bios-install` on a committed MBR and GPT disk
#     image (stage 1 + MBR signature written, partition table preserved) and `enroll-config` on a
#     bootloader file with a checksum slot (hash written into the slot, then `--reset`).
# Upstream's own checks/ suite boots QEMU and needs the full firmware build, so it is not run here.
set -uo pipefail
[ -n "${SOURCE_DATE_EPOCH:-}" ] || unset SOURCE_DATE_EPOCH
: "${SRC:=/mayhem}"
cd "$SRC"

emit_ctrf() {
  local tool="$1" passed="$2" failed="$3" skipped="${4:-0}" pending="${5:-0}" other="${6:-0}"
  local tests=$(( passed + failed + skipped + pending + other ))
  cat > "${CTRF_REPORT:-$SRC/ctrf-report.json}" <<JSON
{
  "results": {
    "tool": { "name": "$tool" },
    "summary": {
      "tests": $tests,
      "passed": $passed,
      "failed": $failed,
      "pending": $pending,
      "skipped": $skipped,
      "other": $other
    }
  }
}
JSON
  printf 'CTRF {"results":{"tool":{"name":"%s"},"summary":{"tests":%d,"passed":%d,"failed":%d,"pending":%d,"skipped":%d,"other":%d}}}\n' \
    "$tool" "$tests" "$passed" "$failed" "$pending" "$skipped" "$other"
  [ "$failed" -eq 0 ]
}

T="$SRC/mayhem/out/test"
for b in probe limine; do
  [ -x "$T/$b" ] || { echo "missing $T/$b — run mayhem/build.sh first" >&2; emit_ctrf limine-known-answer 0 1; exit 1; }
done
WORK="$(mktemp -d "${TMPDIR:-/tmp}/limine-test.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT
pass=0; fail=0
check() {  # name, expected, actual
  if [ "$2" == "$3" ]; then pass=$((pass+1)); echo "PASS $1";
  else fail=$((fail+1)); echo "FAIL $1"; echo "  expected: $2"; echo "  actual:   $3"; fi
}
V=mayhem/volume/testsuite

out="$("$T/probe" disk $V/mbr-fat12.img 2>&1)"
check "mbr: table"     "valid_mbr 1 gpt 0"                                   "$(sed -n 1p <<<"$out")"
check "mbr: partition" "part 1 first 2048 count 2048 label LIMINETEST"       "$(grep '^part' <<<"$out")"
check "mbr: fat file"  "  /limine.conf: 76 bytes: timeout: 3"                "$(grep 'limine.conf:' <<<"$out")"
check "mbr: fat lfn"   "  /boot/A-Long-File-Name.txt: 28 bytes: hello from a long file name" "$(grep 'Long-File' <<<"$out")"

out="$("$T/probe" disk $V/gpt-fat12.img 2>&1)"
check "gpt: table"     "valid_mbr 1 gpt 1"                                   "$(sed -n 1p <<<"$out")"
check "gpt: partition" "part 1 first 2048 count 1952 label GPTFAT"           "$(grep '^part' <<<"$out")"
check "gpt: fat lfn"   "  /boot/A-Long-File-Name.txt: 28 bytes: hello from a long file name" "$(grep 'Long-File' <<<"$out")"

out="$("$T/probe" elf mayhem/elf/testsuite/multiboot-x86_64.elf 2>&1)"
check "elf64: class+entry" "bits 64|entry 0x100000 ranges 4" "$(sed -n 1p <<<"$out")|$(sed -n 2p <<<"$out")"
out="$("$T/probe" elf mayhem/elf/testsuite/kernel-x86_64.elf 2>&1)"
# extent 0xffffffff80001000.. exceeds the 4 GiB multiboot ceiling: Limine must refuse it
check "elf64: higher-half refused" "PANIC: elf: Image extent exceeds the load limit" "$(grep PANIC <<<"$out")"

# gzip layer (the `$` URI prefix): decompressed size and content
out="$("$T/probe" gzip mayhem/gzip/testsuite/limine.conf.gz 2>&1)"
check "gzip: limine.conf" "size 1393 first-line # Some example macros" "$out"
out="$("$T/probe" gzip mayhem/gzip/testsuite/kernel.elf.gz 2>&1)"
check "gzip: kernel" "size 8568 first-line .ELF" "$(head -c 25 <<<"$out" | tr -c 'a-zA-Z0-9 :-' '.')"

# wallpaper decoders: dimensions and corner pixels (xRGB)
check "image: bmp"  "image 8x6 bpp 32 first 000000 last e0c8f0" "$("$T/probe" image mayhem/image/testsuite/gradient.bmp 2>&1)"
check "image: qoi"  "image 8x6 bpp 32 first 000000 last e0c8f0" "$("$T/probe" image mayhem/image/testsuite/gradient.qoi 2>&1)"
check "image: jpeg" "image 1152x768" "$("$T/probe" image mayhem/image/testsuite/bg.jpg 2>&1 | cut -d' ' -f1-2)"
check "image: png"  "image 200x200"  "$("$T/probe" image mayhem/image/testsuite/logo.png 2>&1 | cut -d' ' -f1-2)"

# limine.conf: found on the volume, menu tree built, macros expanded
out="$("$T/probe" config mayhem/test/config-fat12.img 2>&1)"
check "config: found"   "init 0|timeout 3" "$(sed -n 1p <<<"$out")|$(sed -n 2p <<<"$out")"
check "config: entries" "5" "$(grep -c '^entry ' <<<"$out")"
check "config: entry 2" "entry Multiboot2 Test protocol multiboot2" "$(grep '^entry ' <<<"$out" | sed -n 2p)"
check "config: sub-entry" "  sub Multiboot1 Test kernel_path boot():/boot/multiboot.elf" "$(grep '  sub' <<<"$out")"
out="$("$T/probe" config mayhem/test/config-macros.img 2>&1)"
check "config: macro expansion" "  sub Sub kernel_path boot():/kernel" "$(grep '  sub' <<<"$out")"
# the same parser on raw text through init_config_smbios() (the `config` fuzz target's entry)
out="$("$T/probe" conftext mayhem/config/testsuite/example.conf 2>&1)"
check "conftext: parsed" "init 0|timeout 3|5" "$(sed -n 1p <<<"$out")|$(sed -n 2p <<<"$out")|$(grep -c '^entry ' <<<"$out")"
check "conftext: sub-entry" "  sub Multiboot1 Test kernel_path boot():/boot/multiboot.elf" "$(grep '  sub' <<<"$out")"
out="$("$T/probe" conftext mayhem/config/testsuite/crlf.conf 2>&1)"
check "conftext: crlf + macros" "entry Group protocol (none)|  sub Child kernel_path boot():/k" "$(grep '^entry ' <<<"$out" | sed -n 1p)|$(grep '  sub' <<<"$out")"
out="$("$T/probe" conftext mayhem/config/testsuite/nesting.conf 2>&1)"
check "conftext: 64-deep nesting accepted" "init 0" "$(sed -n 1p <<<"$out")"
out="$(printf '/A\n    path: x\n///Orphan\n' > "$WORK/orphan.conf"; "$T/probe" conftext "$WORK/orphan.conf" 2>&1)"
check "conftext: parentless child refused" "PANIC: config: Malformed config file. Parentless child." "$(grep PANIC <<<"$out")"

# bios-install on an MBR disk: stage 1 lands in sector 0 and the partition table stays.
cp $V/mbr-fat12.img "$WORK/mbr.img"
"$T/limine" bios-install --quiet "$WORK/mbr.img" >/dev/null 2>&1; rc=$?
check "bios-install mbr: exit" "0" "$rc"
check "bios-install mbr: boot signature" "55aa" "$(od -An -tx1 -j510 -N2 "$WORK/mbr.img" | tr -d ' \n')"
check "bios-install mbr: partition entry kept, marked active" \
  "80$(od -An -tx1 -j447 -N15 $V/mbr-fat12.img | tr -d ' \n')" "$(od -An -tx1 -j446 -N16 "$WORK/mbr.img" | tr -d ' \n')"
check "bios-install mbr: stage1 written" "1" \
  "$(cmp -s <(head -c 446 $V/mbr-fat12.img) <(head -c 446 "$WORK/mbr.img") && echo 0 || echo 1)"
check "bios-install mbr: fat data intact" \
  "$(tail -c +$((2048*512+1)) $V/mbr-fat12.img | sha256sum)" "$(tail -c +$((2048*512+1)) "$WORK/mbr.img" | sha256sum)"

# bios-install on a GPT disk with no BIOS boot partition: refused, and the undo leaves it unchanged.
cp $V/gpt-fat12.img "$WORK/gpt.img"
err="$("$T/limine" bios-install "$WORK/gpt.img" 2>&1 >/dev/null)"; rc=$?
check "bios-install gpt: refused" "1" "$rc"
check "bios-install gpt: reason" "1" "$(grep -c 'no BIOS boot partition specified' <<<"$err")"
check "bios-install gpt: image unchanged" "$(sha256sum < $V/gpt-fat12.img)" "$(sha256sum < "$WORK/gpt.img")"

# enroll-config: the hash lands right after the signature; --reset puts the slot back.
H=$(printf 'ab%.0s' $(seq 64))
cp mayhem/test/boot-slot.bin "$WORK/boot.sys"
"$T/limine" enroll-config --quiet "$WORK/boot.sys" "$H" >/dev/null 2>&1; rc=$?
check "enroll-config: exit" "0" "$rc"
check "enroll-config: hash written" "$H" "$(dd if="$WORK/boot.sys" bs=1 skip=$((300+26)) count=128 status=none)"
"$T/limine" enroll-config --quiet --reset "$WORK/boot.sys" >/dev/null 2>&1; rc=$?
check "enroll-config --reset: exit" "0" "$rc"
check "enroll-config --reset: slot restored" "$(sha256sum < mayhem/test/boot-slot.bin)" "$(sha256sum < "$WORK/boot.sys")"

emit_ctrf limine-known-answer "$pass" "$fail"
