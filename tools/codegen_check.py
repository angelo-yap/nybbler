#!/usr/bin/env python3
"""Backend codegen guard: assert the lowering actually reaches SIMD.

The IR-level shape tests prove the pass emits a byte-carrier sequence; they say
nothing about what the *backend* does with it. This harness closes that gap by
taking every operation at every field width through the full pipeline

    opt -passes=nybbler  ->  llc -mtriple=x86_64 -mattr=+avx2

and asserting, on the emitted assembly, that

  1. no scalarization survives -- the per-element insert/extract family
     (`vpinsrb`, `vpextrb`, ...) that LLVM's legalizer emits when it gives up on
     a narrow vector must be absent entirely;
  2. the result is genuinely vector code -- at least one xmm/ymm instruction;
  3. the masks are folded, not computed -- every mask must appear as a
     rip-relative constant or a `vpbroadcast` of one, never as a per-iteration
     shift/or chain.

It also records the instruction count against the *unlowered* module compiled by
the same llc, which is the "instruction counts drop versus naive emission"
number (the default legalizer's scalarized expansion is the naive baseline).

## On kernel shape

The kernels here load their operands from memory rather than taking them as
`<K x iN>` arguments, and this is load-bearing rather than incidental. LLVM
legalizes a narrow vector *at the ABI boundary* by promoting each field to its
own byte lane, so a `<32 x i4>` parameter arrives unpacked -- one nibble per
byte of a ymm register. The pass' carrier is the *packed* form (`bitcast
<32 x i4> to <16 x i8>`), so a function-argument kernel pays for a 32-element
repack on entry that swamps the lowering and scalarizes regardless of how good
the body is.

Packed narrow fields come from memory in real use -- that is the layout a
bit-stream buffer already has -- so the memory-shaped kernel is what the pass is
actually for, and the one whose codegen is worth asserting on. Argument-shaped
kernels stay in test/shape/ where only the IR form is being checked.

Usage:
  codegen_check.py --opt OPT --llc LLC --plugin LIBNYBBLER.so [--report]
"""

import argparse
import re
import subprocess
import sys
import tempfile

CARRIER_BYTES = 16  # 128-bit carrier: <32 x i4>, <64 x i2>, <128 x i1>
WIDTHS = [1, 2, 4]

# opcode -> how to spell it in IR. Comparisons carry their predicate.
BINOPS = ["and", "or", "xor", "add", "sub", "shl", "lshr", "ashr"]
CMPOPS = ["eq", "ne", "ult", "slt"]

# The legalizer's tell: per-element inserts/extracts. If any of these appear the
# narrow vector was taken apart field by field, which is exactly what the pass
# exists to prevent.
SCALARIZE_RE = re.compile(r"\b(v?p(?:insr|extr)[bwdq])\b")

# A constant reaching the backend properly shows up as a constant-pool
# reference, an instruction immediate, or one of the register idioms below.
CONST_POOL_REF_RE = re.compile(r"\.LCPI\d+_\d+\(%rip\)")
IMMEDIATE_RE = re.compile(r"^\$-?\d+$")

# all-ones (`vpcmpeqd %r,%r,%r`) and all-zeros (`vpxor %r,%r,%r`) are built from
# a register against itself rather than loaded; both are single instructions
# with no operand dependency, so they count as folded.
IDIOM_OPCODES = ("vpcmpeq", "vpxor")

# Instructions that would be *computing* a value rather than loading one. If
# every operand of one of these is already constant, the backend is building a
# constant at runtime -- exactly the "materialized per iteration" failure.
ALU_PREFIXES = ("vpand", "vpor", "vpxor", "vpadd", "vpsub", "vpsll", "vpsrl",
                "vpsra", "vpshuf", "vpblend", "vpcmp")

# Instructions that legitimately produce a constant register from the pool.
CONST_LOAD_PREFIXES = ("vpbroadcast", "vmovdqa", "vmovdqu", "vmovap", "vmovup")


def kernel(op, n):
    """Emit a memory-shaped kernel for `op` at field width `n`."""
    k = CARRIER_BYTES * 8 // n
    narrow = "<%d x i%d>" % (k, n)
    carrier = "<%d x i8>" % CARRIER_BYTES

    if op in CMPOPS and n > 1:
        # The field-mask idiom: compare, then sext the result back to the
        # operand width and keep using it as a mask. This is the shape narrow
        # compares are actually written in, and the one the pass can keep
        # packed -- see the module docstring on the <K x i1> boundary.
        body = ("  %c = icmp " + op + " " + narrow + " %va, %vb\n"
                "  %r = sext <" + str(k) + " x i1> %c to " + narrow)
        res_ty = narrow
        out_ty = carrier
    elif op in CMPOPS:
        # At i1 the compare result already *is* the field type: no sext exists
        # to fold, and the <K x i1> result is itself the packed bitmask.
        body = "  %r = icmp " + op + " " + narrow + " %va, %vb"
        res_ty = narrow
        out_ty = carrier
    else:
        body = "  %%r = %s %s %%va, %%vb" % (op, narrow)
        res_ty = narrow
        out_ty = carrier

    return """
define void @{name}(ptr %pa, ptr %pb, ptr %pr) {{
  %ba = load {carrier}, ptr %pa
  %bb = load {carrier}, ptr %pb
  %va = bitcast {carrier} %ba to {narrow}
  %vb = bitcast {carrier} %bb to {narrow}
{body}
  %br = bitcast {res} %r to {out}
  store {out} %br, ptr %pr
  ret void
}}
""".format(name="%s_i%d" % (op, n), carrier=carrier, narrow=narrow, body=body,
           res=res_ty, out=out_ty)


def computed_constants(instrs):
    """Find masks the backend had to build at runtime.

    Walks the instruction list tracking which vector registers currently hold a
    constant -- seeded by constant-pool loads/broadcasts and the all-ones/
    all-zeros register idioms. Any ALU instruction whose operands are *all*
    constant is computing a constant instead of loading a folded one, which is
    the regression this guards against. Returns the offending instructions.
    """
    const_regs = set()
    offenders = []

    for ins in instrs:
        parts = ins.split(None, 1)
        opcode = parts[0]
        operands = [o.strip() for o in parts[1].split(",")] if len(parts) > 1 else []
        if not operands:
            continue
        srcs, dst = operands[:-1], operands[-1]

        def is_const(o):
            return (CONST_POOL_REF_RE.search(o) or IMMEDIATE_RE.match(o)
                    or o in const_regs)

        # `vpxor %r,%r,%r` / `vpcmpeqd %r,%r,%r`: a constant from nothing.
        if opcode.startswith(IDIOM_OPCODES) and len(set(operands)) == 1:
            const_regs.add(dst)
            continue

        if opcode.startswith(CONST_LOAD_PREFIXES):
            (const_regs.add if srcs and all(is_const(s) for s in srcs)
             else const_regs.discard)(dst)
            continue

        if srcs and all(is_const(s) for s in srcs):
            if opcode.startswith(ALU_PREFIXES):
                offenders.append(ins)
            const_regs.add(dst)
        else:
            const_regs.discard(dst)

    return offenders


def instructions(asm):
    """The instruction lines of an assembly listing (no directives/labels)."""
    out = []
    for line in asm.splitlines():
        stripped = line.strip()
        if not stripped or stripped.startswith((".", "#")) or stripped.endswith(":"):
            continue
        out.append(stripped.split("#")[0].strip())
    return out


def compile_asm(src, opt, llc, plugin, lower):
    """Run one kernel through the pipeline; return the emitted assembly."""
    with tempfile.NamedTemporaryFile("w", suffix=".ll", delete=False) as f:
        f.write(src)
        path = f.name

    def run(cmd, **kw):
        r = subprocess.run(cmd, capture_output=True, text=True, **kw)
        if r.returncode != 0:
            sys.exit("%s failed:\n%s" % (" ".join(cmd), r.stderr))
        return r.stdout

    ir = src
    if lower:
        ir = run([opt, "-load-pass-plugin", plugin, "-passes=nybbler", path, "-S"])

    asm = run([llc, "-mtriple=x86_64-unknown-linux-gnu", "-mattr=+avx2", "-o", "-"],
              input=ir)
    return ir, asm


def check(op, n, opt, llc, plugin):
    """Returns (errors, lowered_count, baseline_count)."""
    src = kernel(op, n)
    lowered_ir, lowered = compile_asm(src, opt, llc, plugin, lower=True)
    _, baseline = compile_asm(src, opt, llc, plugin, lower=False)

    errs = []
    lo = instructions(lowered)

    scalarized = SCALARIZE_RE.findall(lowered)
    if scalarized:
        errs.append("scalarized: %s" % ", ".join(sorted(set(scalarized))))

    if not any("%xmm" in i or "%ymm" in i for i in lo):
        errs.append("no vector instructions emitted")

    # Masks must arrive as folded constants, not be rebuilt with ALU ops.
    for ins in computed_constants(lo):
        errs.append("mask computed at runtime: %s" % ins)

    return errs, len(lo), len(instructions(baseline))


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--opt", required=True)
    p.add_argument("--llc", required=True)
    p.add_argument("--plugin", required=True)
    p.add_argument("--report", action="store_true",
                   help="print the full instruction-count table")
    args = p.parse_args()

    failures = []
    rows = []
    for op in BINOPS + CMPOPS:
        for n in WIDTHS:
            errs, lo, base = check(op, n, args.opt, args.llc, args.plugin)
            rows.append((op, n, lo, base))
            for e in errs:
                failures.append("%s i%d: %s" % (op, n, e))

    if args.report:
        print("%-6s %-5s %9s %9s %8s"
              % ("op", "width", "nybbler", "baseline", "ratio"))
        for op, n, lo, base in rows:
            ratio = "%.1fx" % (base / lo) if lo else "-"
            print("%-6s i%-3d %9d %9d %8s" % (op, n, lo, base, ratio))
        tot_lo = sum(r[2] for r in rows)
        tot_base = sum(r[3] for r in rows)
        print("%-6s %-4s %9d %9d %8s"
              % ("TOTAL", "", tot_lo, tot_base, "%.1fx" % (tot_base / tot_lo)))

    if failures:
        for f in failures:
            print("CODEGEN FAIL:", f)
        return 1

    print("CODEGEN OK (%d kernels: SIMD emitted, no scalarization, masks folded)"
          % len(rows))
    return 0


if __name__ == "__main__":
    sys.exit(main())
