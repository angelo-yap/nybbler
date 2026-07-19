#!/usr/bin/env python3
"""Differential test harness for the Nybbler lowering pass.

For each `define <K x iN> @kernel(<K x iN>, <K x iN>)` found in the input file,
this runs a battery of input pairs (structured edge cases + randomized) through
two paths and asserts the results are bit-identical:

  * reference  -- the *unlowered* module executed by lli; LLVM legalizes the
                  narrow-field op by scalarizing it, giving ground-truth
                  per-field semantics.
  * candidate  -- the same module after `opt -passes=nybbler`, i.e. the SWAR
                  carrier lowering, executed by lli.

The reference is therefore a genuine scalar reference with no per-operation code
here: the harness is fully op-agnostic, so adding a new operation is just a new
kernel file -- no harness changes (see the test-suite ticket "Done when").

Non-byte-multiple kernels (K*N % 8 != 0, exercising the pass' zero-padding
path) are supported: their operands are spelled as literal <K x iN> constants
and their results printed per-field, since no legal byte bitcast exists.

Reproducibility: the RNG seed defaults to 42 and can be overridden with the
NYBBLER_DIFF_SEED environment variable (CI sets a random one); the seed actually
used is printed so any CI failure can be reproduced exactly.

Usage:
  diff_runner.py --opt OPT --lli LLI --plugin LIBNYBBLER.so KERNEL.ll
Prints per-kernel results and a final "ALL PASS" line iff everything matched.
"""

import argparse
import os
import random
import re
import subprocess
import sys
import tempfile

# `define <K x iN> @name(` -- capture the field geometry and the kernel name.
KERNEL_RE = re.compile(r"define\s+<(\d+)\s+x\s+i(\d+)>\s+@([A-Za-z0-9_.]+)\s*\(")

# A few structured fill bytes covering all-zero, all-one, alternating-bit, and
# signed-boundary (high bit) field patterns regardless of field width.
STRUCTURED = [0x00, 0xFF, 0xAA, 0x55, 0x80, 0x7F]
NUM_RANDOM = 100


def i8(v):
    """Render an unsigned byte as a signed i8 literal LLVM's parser accepts."""
    return v - 256 if v > 127 else v


def trials(nbytes, rng):
    """Yield (a_bytes, b_bytes) input pairs of length `nbytes`."""
    # Structured x structured: all-zero/all-one/alternating/sign-boundary fields.
    for fa in STRUCTURED:
        for fb in STRUCTURED:
            yield [fa] * nbytes, [fb] * nbytes
    # Randomized pairs (reproducible via the seed).
    for _ in range(NUM_RANDOM):
        yield ([rng.randrange(256) for _ in range(nbytes)],
               [rng.randrange(256) for _ in range(nbytes)])


def fields(bytez, K, N):
    """Slice the little-endian packed `bytez` into K N-bit field values."""
    mask = (1 << N) - 1
    return [(bytez[(i * N) // 8] >> ((i * N) % 8)) & mask for i in range(K)]


def vec_const(bytez, K, N):
    """A `<K x iN>` operand built by bitcasting an `<M x i8>` constant.

    Non-byte-multiple vectors (K*N % 8 != 0) have no legal byte bitcast, so
    their fields are spelled directly as an `<K x iN>` literal instead; the
    trailing bits of the last trial byte simply go unused."""
    if (K * N) % 8 != 0:
        elems = ", ".join("i%d %d" % (N, v) for v in fields(bytez, K, N))
        return "<%s>" % elems
    elems = ", ".join("i8 %d" % i8(v) for v in bytez)
    M = len(bytez)
    return "bitcast (<%d x i8> <%s> to <%d x i%d>)" % (M, elems, K, N)


def build_module(src, kernels, batteries):
    """Combined module: original kernels + one small @trialN function per
    trial (lli's interpreter scales poorly with function size, so a single
    @main unrolling every trial is superlinear -- see diff_runner perf note)
    plus a @main that calls each in turn and prints its result bytes."""
    out = [src, "",
           '@.hex = private constant [5 x i8] c"%02x\\00"',
           '@.nl  = private constant [2 x i8] c"\\0A\\00"',
           "declare i32 @printf(i8*, ...)"]
    uid = 0
    for (K, N, name), battery in zip(kernels, batteries):
        byte_multiple = (K * N) % 8 == 0
        M = K * N // 8
        for (a, b) in battery:
            fn = ["define void @trial{0}() {{".format(uid)]
            fn.append("  %r = call <{K} x i{N}> @{name}(<{K} x i{N}> {av}, "
                       "<{K} x i{N}> {bv})".format(
                           K=K, N=N, name=name,
                           av=vec_const(a, K, N), bv=vec_const(b, K, N)))
            if byte_multiple:
                fn.append("  %rb = bitcast <{K} x i{N}> %r to <{M} x i8>"
                           .format(K=K, N=N, M=M))
                for j in range(M):
                    fn.append("  %e{0} = extractelement <{M} x i8> %rb, i32 {0}"
                               .format(j, M=M))
                    fn.append("  %z{0} = zext i8 %e{0} to i32".format(j))
                    fn.append("  call i32 (i8*, ...) @printf(i8* getelementptr("
                               "[5 x i8], [5 x i8]* @.hex, i32 0, i32 0), i32 %z{0})"
                               .format(j))
            else:
                # No legal byte bitcast: print each field instead. Both the
                # reference and the candidate run this same module, so the
                # formats always line up.
                for j in range(K):
                    fn.append("  %e{0} = extractelement <{K} x i{N}> %r, i32 {0}"
                               .format(j, K=K, N=N))
                    fn.append("  %z{0} = zext i{N} %e{0} to i32".format(j, N=N))
                    fn.append("  call i32 (i8*, ...) @printf(i8* getelementptr("
                               "[5 x i8], [5 x i8]* @.hex, i32 0, i32 0), i32 %z{0})"
                               .format(j))
            fn.append("  call i32 (i8*, ...) @printf(i8* getelementptr("
                       "[2 x i8], [2 x i8]* @.nl, i32 0, i32 0))")
            fn.append("  ret void")
            fn.append("}")
            out.append("\n".join(fn))
            uid += 1
    main = ["define i32 @main() {"]
    for i in range(uid):
        main.append("  call void @trial{0}()".format(i))
    main += ["  ret i32 0", "}"]
    out.append("\n".join(main))
    return "\n".join(out)


def run_lli(lli, path):
    return subprocess.run([lli, path], capture_output=True, text=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--opt", required=True)
    ap.add_argument("--lli", required=True)
    ap.add_argument("--plugin", required=True)
    ap.add_argument("kernel")
    args = ap.parse_args()

    src = open(args.kernel).read()
    kernels = [(int(k), int(n), name) for k, n, name in KERNEL_RE.findall(src)]
    if not kernels:
        print("NO KERNELS FOUND in %s" % args.kernel)
        return 1

    seed = int(os.environ.get("NYBBLER_DIFF_SEED", "42"))
    print("seed=%d  kernels=%s" % (seed, ",".join(k[2] for k in kernels)))
    rng = random.Random(seed)
    # ceil-divide so non-byte-multiple kernels get enough trial bytes to fill
    # every field (the last byte's leftover bits go unused).
    batteries = [list(trials((K * N + 7) // 8, rng)) for (K, N, _) in kernels]

    with tempfile.TemporaryDirectory() as d:
        raw = os.path.join(d, "m.ll")
        with open(raw, "w") as f:
            f.write(build_module(src, kernels, batteries))

        # candidate = nybbler-lowered module; reference = raw module.
        low = os.path.join(d, "m.low.ll")
        opt = subprocess.run(
            [args.opt, "-load-pass-plugin", args.plugin, "-passes=nybbler",
             raw, "-S", "-o", low], capture_output=True, text=True)
        if opt.returncode != 0:
            print("OPT FAILED\n" + opt.stderr)
            return 1

        ref, cand = run_lli(args.lli, raw), run_lli(args.lli, low)
        if ref.returncode != 0:
            print("REFERENCE lli FAILED\n" + ref.stderr)
            return 1
        if cand.returncode != 0:
            print("CANDIDATE lli FAILED\n" + cand.stderr)
            return 1

    ref_lines, cand_lines = ref.stdout.splitlines(), cand.stdout.splitlines()
    # Map each output line back to its (kernel, trial) for a precise diff.
    index = [(kn[2], ti, a, b)
             for kn, bat in zip(kernels, batteries)
             for ti, (a, b) in enumerate(bat)]

    ok = True
    if len(ref_lines) != len(cand_lines):
        print("LINE COUNT MISMATCH ref=%d cand=%d"
              % (len(ref_lines), len(cand_lines)))
        ok = False
    for n, (r, c) in enumerate(zip(ref_lines, cand_lines)):
        if r != c:
            ok = False
            if n < len(index):
                name, ti, a, b = index[n]
                print("MISMATCH %s trial=%d a=%s b=%s ref=%s cand=%s"
                      % (name, ti, bytes(a).hex(), bytes(b).hex(), r, c))
            else:
                print("MISMATCH line=%d ref=%s cand=%s" % (n, r, c))

    if ok:
        print("ALL PASS (%d kernels, %d trials)"
              % (len(kernels), len(index)))
        return 0
    print("DIFFERENTIAL FAILED")
    return 1


if __name__ == "__main__":
    sys.exit(main())
