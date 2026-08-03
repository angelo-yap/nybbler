#!/usr/bin/env bash
#
# nybbler end-to-end demo: source IR -> opt -passes=nybbler -> llc -> run.
#
# Shows the two things that matter, in order:
#   1. what the backend does with narrow-field IR on its own (scalarization),
#   2. what it does after nybbler has lowered it (SIMD),
# then builds the lowered kernels for the host and runs them against a scalar
# reference so the speed claim is backed by a correctness check.
#
# The assembly comparison targets x86-64 + AVX2 (the platform we report
# numbers for); the executable is built for whatever host you are on, so this
# runs unchanged on an arm64 laptop and an x86-64 CI box.
#
# Tool paths can be overridden:  OPT=... LLC=... CC=... ./demo/run.sh

set -euo pipefail
cd "$(dirname "$0")"

OPT=${OPT:-opt-22}
LLC=${LLC:-llc-22}
CC=${CC:-clang-22}
PLUGIN=${PLUGIN:-../build/libNybbler.so}

if [ ! -f "$PLUGIN" ]; then
  echo "error: plugin not found at $PLUGIN" >&2
  echo "build it first:  cmake -S . -B build -DLLVM_DIR=... && cmake --build build -j" >&2
  exit 1
fi

WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

TARGET="-mtriple=x86_64-unknown-linux-gnu -mattr=+avx2"

# Instructions in one function of an assembly listing: drop directives,
# comments, labels, and the constant pool.
count_fn() {
  awk -v fn="$2" '$0 ~ "^" fn ":" {inside=1; next}
                  /^\.Lfunc_end/ {inside=0}
                  inside && !/^[[:space:]]*[.#]/ && !/:$/ && NF' "$1" | wc -l
}

echo "== 1. the source =========================================================="
echo "Ordinary IR, except the vectors have 4-bit fields no target can execute:"
grep -E "add <32 x i4>|icmp ult <32 x i4>|sext <32 x i1>" kernels.ll | sed 's/^/    /'

echo
echo "== 2. what nybbler emits =================================================="
"$OPT" -load-pass-plugin "$PLUGIN" -passes=nybbler kernels.ll -S > "$WORK/lowered.ll"
echo "The <32 x i4> add becomes a byte-carrier SWAR sequence:"
sed -n '/^loop:/,/^exit:/p' "$WORK/lowered.ll" | grep -E "^  %(add|.*bitcast)" | head -8 | sed 's/^/    /'

echo
echo "== 3. x86-64 AVX2 codegen, before and after ==============================="
"$LLC" $TARGET kernels.ll        -o "$WORK/baseline.s"
"$LLC" $TARGET "$WORK/lowered.ll" -o "$WORK/lowered.s"

printf "    %-18s %10s %10s %8s\n" kernel baseline nybbler ratio
for fn in nibble_add nibble_lt_mask; do
  base=$(count_fn "$WORK/baseline.s" "$fn")
  low=$(count_fn "$WORK/lowered.s" "$fn")
  printf "    %-18s %10s %10s %7sx\n" "$fn" "$base" "$low" \
    "$(awk -v b="$base" -v l="$low" 'BEGIN { printf "%.1f", b/l }')"
done

echo
echo "    baseline scalarizes -- per-nibble extract/insert:"
grep -oE "vp(insr|extr)[bwdq]" "$WORK/baseline.s" | sort | uniq -c \
  | sed 's/^/      /' | head -4
echo "    nybbler does not:"
echo "      $(grep -cE 'vp(insr|extr)[bwdq]' "$WORK/lowered.s" || true) insert/extract instructions"
echo "    and the loop-invariant field masks are hoisted out of the loop body,"
echo "    not rebuilt per iteration:"
sed -n "/^nibble_add:/,/^\.Lfunc_end/p" "$WORK/lowered.s" \
  | grep -E "vpbroadcast|LCPI" | head -3 | sed 's/^/      /'

echo
echo "== 4. build and run for the host =========================================="
"$OPT" -load-pass-plugin "$PLUGIN" -passes=nybbler kernels.ll -S \
  | "$LLC" -filetype=obj -o "$WORK/kernels.o"
"$CC" -O2 main.c "$WORK/kernels.o" -o "$WORK/demo"
"$WORK/demo" | sed 's/^/    /'
