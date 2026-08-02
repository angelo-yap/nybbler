#!/usr/bin/env bash
#
# run.sh -- build every benchmark kernel two ways and compare them.
#
#   baseline : llc alone. The narrow-field op hits the default legalizer,
#              which scalarizes it.
#   lowered  : opt -passes=nybbler first, then the *identical* llc invocation.
#
# Holding the llc line fixed across both sides is the whole fairness argument:
# the only difference between the two binaries is nybbler. See
# docs/benchmarks.md for the full methodology.
#
# Usage:
#   bash bench/run.sh                 build, verify checksums, time, print table
#   bash bench/run.sh --check-only    build and verify checksums only (CI path)
#
# Tool paths can be overridden: OPT=, LLC=, CLANG=, PLUGIN=.

set -euo pipefail

BENCH_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(dirname "$BENCH_DIR")"

OPT="${OPT:-opt-22}"
LLC="${LLC:-llc-22}"
CLANG="${CLANG:-clang-22}"
PLUGIN="${PLUGIN:-$ROOT_DIR/build/libNybbler.so}"

# -mcpu=native gives the host's real SIMD width. Overridable so CI runners and
# other machines can pin something reproducible.
MCPU="${MCPU:--mcpu=native}"

CHECK_ONLY=0
[[ "${1:-}" == "--check-only" ]] && CHECK_ONLY=1

OUT_DIR="$BENCH_DIR/build"
mkdir -p "$OUT_DIR"

if [[ ! -f "$PLUGIN" ]]; then
    echo "error: plugin not found at $PLUGIN" >&2
    echo "       build it first: cmake --build build -j" >&2
    exit 1
fi

for tool in "$OPT" "$LLC" "$CLANG"; do
    command -v "$tool" >/dev/null 2>&1 || {
        echo "error: $tool not found on PATH" >&2
        exit 1
    }
done

# Read a `; NYB_KEY: value` header comment out of a kernel file.
kernel_meta() {
    sed -n "s/^; *$2: *//p" "$1" | head -1
}

status=0
declare -a rows=()

for kernel in "$BENCH_DIR"/kernels/*.ll; do
    name="$(basename "$kernel" .ll)"
    out_bytes="$(kernel_meta "$kernel" NYB_OUT_BYTES_PER_VEC)"
    out_bytes="${out_bytes:-16}"

    # --- baseline: straight to llc, narrow op scalarized by the legalizer ---
    "$LLC" -O3 $MCPU -filetype=obj "$kernel" -o "$OUT_DIR/$name.base.o"
    "$LLC" -O3 $MCPU "$kernel" -o "$OUT_DIR/$name.base.s"

    # --- lowered: nybbler, then the same llc line ---
    "$OPT" -load-pass-plugin "$PLUGIN" -passes=nybbler "$kernel" -S \
        -o "$OUT_DIR/$name.low.ll"
    "$LLC" -O3 $MCPU -filetype=obj "$OUT_DIR/$name.low.ll" -o "$OUT_DIR/$name.low.o"
    "$LLC" -O3 $MCPU "$OUT_DIR/$name.low.ll" -o "$OUT_DIR/$name.low.s"

    for variant in base low; do
        "$CLANG" -O2 -std=c11 \
            -DNYB_OUT_BYTES_PER_VEC="$out_bytes" \
            -DNYB_NAME="\"$name\"" \
            "$BENCH_DIR/driver.c" "$OUT_DIR/$name.$variant.o" \
            -o "$OUT_DIR/$name.$variant"
    done

    # --- correctness gate ---
    #
    # A kernel that is fast but computes something different is not a result.
    # The checksums must agree before any timing is reported.
    base_sum="$("$OUT_DIR/$name.base" --check-only | awk '{print $2}')"
    low_sum="$("$OUT_DIR/$name.low" --check-only | awk '{print $2}')"

    if [[ "$base_sum" != "$low_sum" ]]; then
        echo "CHECKSUM MISMATCH $name baseline=$base_sum lowered=$low_sum"
        status=1
        continue
    fi
    echo "CHECKSUM MATCH $name $base_sum"

    if [[ $CHECK_ONLY -eq 1 ]]; then
        continue
    fi

    base_ns="$("$OUT_DIR/$name.base" | sed -n 's/.*[^_]ns_per_vec=\([0-9.]*\).*/\1/p')"
    low_ns="$("$OUT_DIR/$name.low"  | sed -n 's/.*[^_]ns_per_vec=\([0-9.]*\).*/\1/p')"
    speedup="$(awk -v b="$base_ns" -v l="$low_ns" 'BEGIN{ printf "%.2f", (l>0)? b/l : 0 }')"
    rows+=("$(printf '%-12s %14s %14s %10sx' "$name" "$base_ns" "$low_ns" "$speedup")")
done

if [[ $CHECK_ONLY -eq 0 && ${#rows[@]} -gt 0 ]]; then
    echo
    printf '%-12s %14s %14s %11s\n' kernel "baseline ns/v" "lowered ns/v" speedup
    printf '%s\n' "-------------------------------------------------------------"
    printf '%s\n' "${rows[@]}"
    echo
    echo "baseline = llc -O3 $MCPU alone (narrow op scalarized)"
    echo "lowered  = opt -passes=nybbler, then the identical llc line"
    echo "ns/v     = nanoseconds per 16-byte carrier vector, min of 50 reps"
fi

exit $status
