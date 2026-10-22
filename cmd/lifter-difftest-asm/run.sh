#!/bin/bash
# Differential test of the ARM64 assembly lifter against x86-64 under Rosetta 2.
#
# Usage: run.sh <build dir | guest_image.bin> <listing | -> <workdir> [compare.py flags]
#
# With a build directory (the output of ps4-recomp), the guest image is <dir>/guest_image.bin and,
# when the listing argument is "-", the listing is taken from the recompiler's own disassembly:
# the "// 0xADDR: INST" comment that precedes every lifted instruction in <dir>/code_*.s. That is
# exactly the set of instructions the recompiler lifts for the eboot.
#
# LOCK forms: listing sites with a LOCK prefix are sampled apart from their plain form, and every
# LOCK form in every size (main.go lockForms) is added to each run. The harness is single-threaded:
# it checks values and flags of the atomic lowerings, not atomicity (pkg/lifter TestLockFullBarrier).
#
# Environment: MAX_PER_OP (default 40), SPECIAL=1 adds special FP values (inf/nan/denormals),
# STAGE_TIMEOUT (seconds per stage, default 900), TSO=0 lifts without x86 memory ordering
# (ps4-recomp -tso=false; the default lifts with it, as ps4-recomp does).
#
# Every stage runs under /usr/bin/time -l; the peak resident set size of each stage is written to
# <workdir>/peak_rss.txt (MB) so runs on large eboots can be kept memory-bounded.
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
SRC=$1; DIS=$2; OUT=$3; shift 3
mkdir -p "$OUT"
OUT="$(cd "$OUT" && pwd)"
TMO=${STAGE_TIMEOUT:-900}
: > "$OUT/peak_rss.txt"
# stage NAME CMD...: run CMD with a time limit and record its peak RSS.
stage() {
  local name=$1; shift
  local rc=0
  /usr/bin/time -l -o "$OUT/time_$name.txt" timeout "$TMO" "$@" || rc=$?
  local rss
  rss=$(awk '/maximum resident set size/{printf "%d", $1/1048576}' "$OUT/time_$name.txt")
  echo "$name ${rss:-?} MB (exit $rc)" >> "$OUT/peak_rss.txt"
  return $rc
}
if [ -d "$SRC" ]; then
  IMG="$SRC/guest_image.bin"
  if [ "$DIS" = "-" ]; then
    DIS="$OUT/listing.txt"
    find "$SRC" -maxdepth 1 -name 'code_*.s' -print0 | sort -z | xargs -0 grep -h '^    // 0x' |
      sed -E 's|^    // 0x([0-9a-f]+): |\1: |' > "$DIS"
  fi
else
  IMG="$SRC"
fi
arch -x86_64 /usr/bin/true || { echo "Rosetta 2 is not installed" >&2; exit 1; }
(cd "$ROOT" && go build -o "$OUT/lifter-difftest-asm" ./cmd/lifter-difftest-asm)
stage generator "$OUT/lifter-difftest-asm" -image "$IMG" -dis "$DIS" -out "$OUT" -max-per-op ${MAX_PER_OP:-40} -tso=$([ "${TSO:-1}" = 0 ] && echo false || echo true)
H="$HERE/harness"
stage clang_native clang -arch x86_64 -O1 -I"$H" "$H/native_main.c" "$OUT/native_tests.s" -o "$OUT/native_dt"
stage clang_asm clang -arch arm64 -O1 -w -I"$H" -I"$ROOT/pkg/runtime/core" "$H/asm_main.c" "$OUT/asm_tests.s" -o "$OUT/asm_dt"
stage native_dt "$OUT/native_dt" "$OUT/native.bin" ${SPECIAL:-0}
stage asm_dt "$OUT/asm_dt" "$OUT/asm.bin" ${SPECIAL:-0} 2>/dev/null
stage compare python3 "$H/compare.py" "$OUT/native.bin" "$OUT/asm.bin" "$OUT/tests.txt" --summary "$OUT/summary.md" "$@"
cat "$OUT/peak_rss.txt"
