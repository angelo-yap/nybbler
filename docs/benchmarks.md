# Benchmarks — methodology, results, limitations

What is measured, how, what came out, and what nybbler deliberately does not
cover. The lowerings themselves are described in [`lowering.md`](lowering.md)
and argued correct in [`correctness.md`](correctness.md).

Run everything with:

```bash
cmake --build build -j        # the plugin must exist first
bash bench/run.sh
```

## Methodology

### The two builds

Each kernel is compiled twice from the *same* `.ll` source:

```sh
# baseline -- straight to llc. The narrow-field op reaches the default
# legalizer, which scalarizes it.
llc-22 -O3 -mcpu=native kernels/$K.ll -filetype=obj -o build/$K.base.o

# lowered -- nybbler first, then the identical llc invocation.
opt-22 -load-pass-plugin build/libNybbler.so -passes=nybbler \
       kernels/$K.ll -S -o build/$K.low.ll
llc-22 -O3 -mcpu=native build/$K.low.ll -filetype=obj -o build/$K.low.o
```

Both objects then link against the same `bench/driver.c` compiled with the
same `clang-22 -O2`. **The only difference between the two binaries is the
pass.** Holding the `llc` line fixed on both sides is the entire fairness
argument: the baseline is not a strawman written differently, it is the same
program given to the same backend at the same optimization level.

### Why the loads are `<16 x i8>` and not `<32 x i4>`

Every kernel loads and stores at byte type and bitcasts into the narrow type
only around the operation itself:

```llvm
%la = load <16 x i8>, ptr %pa, align 16
%va = bitcast <16 x i8> %la to <32 x i4>
%r  = add <32 x i4> %va, %vb
%lo = bitcast <32 x i4> %r to <16 x i8>
store <16 x i8> %lo, ptr %po, align 16
```

This is worth stating plainly because the obvious way to write the kernel —
`load <32 x i4>` … `store <32 x i4>` — makes the lowered build measurably
*worse*: 346 → 465 assembly instructions in the probe that motivated this
design. The reason is that nybbler rewrites the **operation** and nothing
else. A narrow-vector `load` is legalized independently, by scalarizing, in
both builds; the lowered build then pays that scalarized load *and* the cost
of repacking the pieces into a byte vector for the carrier op. The memory
traffic swamps and inverts the result.

Keeping memory at byte type isolates what the pass actually claims to fix. It
is not a thumb on the scale — the baseline gets the identical byte-typed
loads, and both builds are free to use them. It is also the shape real
bit-stream code has, since packed narrow data is naturally moved as bytes.
The corresponding constraint on callers is recorded under
[Limitations](#limitations).

### Measurement

- **Buffers.** Three 64-byte-aligned buffers of 256 KiB each, sized to stay
  inside the development machine's 512 KiB per-core L2. At DRAM bandwidth both
  builds would converge on the memory system and the lowering difference would
  vanish into the noise; the point of the benchmark is the compute.
- **Inputs.** Deterministic, from a fixed-seed xorshift64 rather than
  `rand()`, so both builds see byte-identical data and the checksum is
  reproducible across machines and libc versions.
- **Timed region.** The `nyb_kernel` call and nothing else. Allocation, fill,
  and checksum are all outside it. No I/O anywhere in the loop.
- **Repetitions.** 20 warmup passes, then 50 timed passes measured
  individually with `CLOCK_MONOTONIC`.
- **Reported statistic.** The **minimum**, because scheduling and interrupts
  can only add time, never remove it, so the minimum is the least contaminated
  sample. The median is printed alongside so a run where the machine was busy
  throughout is visible rather than silently reported as a clean number.
- **Checksum.** FNV-1a over the whole output buffer, doing two jobs at once:
  it makes the output live so the optimizer cannot delete the loop as dead,
  and it is the correctness gate. `run.sh` compares the baseline and lowered
  checksums and refuses to report timings if they differ — a fast kernel that
  computes the wrong thing is not a result.

### Kernels

| Kernel | Type | Operations |
|---|---|---|
| `arith_i4` | `<32 x i4>` | `add`, `shl` by per-field amount, `sub` |
| `field_i2` | `<64 x i2>` | `add`, `lshr` by per-field amount, `sub` |
| `mask_i1`  | `<128 x i1>` | `xor`, `and`, `or` |
| `cmp_i4`   | `<32 x i4>` | `icmp ult` → packed bitmask (partial, see below) |

Each is a short op *chain* over a stream rather than a single instruction, so
the numbers reflect a realistic sequence with reuse between operations rather
than one isolated instruction's latency.

Shift amounts are masked to `[0, N-1]` in the kernel IR. An amount `>= N` is
poison, which has no defined value for the scalarized baseline to produce, so
the two builds' checksums could legitimately disagree and the run would fail
for a non-reason. Over-width shift behaviour is covered separately by
`test/shift_overwidth.ll`.

### Environment

Numbers below were measured on:

- AMD Ryzen 9 5900HX, x86-64 with AVX2 (`-mcpu=native`), 4 cores visible
- Ubuntu 24.04 under WSL2, Linux 6.6.87.2
- LLVM / clang 22.1.8 from apt.llvm.org

Absolute ns/vector will differ elsewhere; the ratios are the portable part,
and the assembly evidence below explains where they come from.

## Results

```
kernel        baseline ns/v   lowered ns/v     speedup
-------------------------------------------------------------
arith_i4            67.2517         5.0204      13.40x
cmp_i4              48.5020        35.9009       1.35x
field_i2           144.8460         2.4858      58.27x
mask_i1              1.0407         1.0420       1.00x
```

All four kernels report `CHECKSUM MATCH`; the two builds are bit-identical in
output. Ratios were stable to within ~0.03x across repeated runs.

The generated code says the same thing more directly. Counting
scalarization-marker instructions (`vpinsrb`, `vpextrb`, `bextr`) and total
instructions in the inner loop:

| Kernel | baseline markers | baseline loop | lowered markers | lowered loop |
|---|---|---|---|---|
| `arith_i4` | 114 | 343 | **0** | 43 |
| `field_i2` | 236 | 713 | **0** | 26 |
| `mask_i1`  | 0 | 11 | 0 | 11 |
| `cmp_i4`   | 84 | 218 | 42 | 124 |

The lowered `arith_i4` inner loop is pure SIMD, and the mask constants from
[`lowering.md`](lowering.md) are visible in the preamble exactly as derived —
`0x77` (`L` at `N=4`) and `0x88` (`H`) broadcast into registers:

```asm
vpbroadcastb .LCPI0_6(%rip), %xmm0    # 119 = 0x77
vpbroadcastb .LCPI0_7(%rip), %xmm1    # 136 = 0x88
...
.LBB0_2:
    vmovdqa (%rsi,%rax), %xmm7
    vmovdqa (%rdi,%rax), %xmm6
    vpand   %xmm0, %xmm7, %xmm9       # b & L
    vpand   %xmm0, %xmm6, %xmm8       # a & L
    vpaddb  %xmm9, %xmm8, %xmm9       # (a&L) + (b&L)
    vpxor   %xmm7, %xmm6, %xmm10      # a ^ b
    vpand   %xmm1, %xmm10, %xmm10     # (a^b) & H
    vpxor   %xmm10, %xmm9, %xmm10     # add result
    ...
```

### Reading the numbers

**`field_i2` beats `arith_i4` (58x vs 13x) because the baseline is worse, not
because the lowering is better.** Both lower to a comparable handful of SIMD
instructions, but at `N=2` the scalarized baseline has 64 lanes to extract and
re-insert per vector instead of 32. The narrower the field, the more the
default legalizer has to do and the less it gets done — which is the whole
motivation for the pass.

**`mask_i1` shows no speedup, and that is the correct result.** Bitwise
operations are bit-independent, so LLVM's own legalizer already reinterprets
`<K x i1>` bitwise as bytes rather than scalarizing — the baseline loop is
already 11 instructions with zero scalarization markers, and nybbler emits
what the backend would have produced anyway. What the pass adds here is that
the reinterpretation is explicit and guaranteed rather than dependent on the
legalizer's discretion for a given target.

This is worth being clear about rather than burying: **the speedup claim rests
on the masked paths** — `add`, `sub`, `shl`, `lshr`, `ashr` — not on the
bitwise case that is correct by construction. `arith_i4` and `field_i2` are
what measure it.

**`cmp_i4` is a partial result and should be read as a lower bound.** The pass
lowers the `icmp`, but the `bitcast <32 x i1> to <4 x i8>` that materializes
the result into a packed mask is not something the pass touches, and it is
expensive in both builds — 42 scalarization markers remain in the lowered
build, all from the mask materialization. The 1.35x therefore understates the
compare lowering substantially. This cannot be fixed from the kernel side; see
the corresponding limitation below.

## Limitations

What nybbler deliberately does not cover.

**No multiply, divide, or remainder.** `mul`, `udiv`, `sdiv`, `urem`, `srem`
on narrow fields have no handler and fall through to the default legalizer.
There is no cheap SWAR multiply analogous to the add/sub containment tricks —
partial products cross field boundaries by construction — so this is a real
scope decision rather than an oversight.

**Vertical operations only.** The pass handles field-parallel operations where
output field *i* depends only on input fields *i*. Horizontal reductions,
shuffles, and anything else that moves data between lanes are out of scope,
and the correctness argument in [`correctness.md`](correctness.md) — which is
entirely about keeping effects inside a field — does not extend to them.

**Comparison predicates are limited to `eq`, `ne`, `ult`, `slt`.**
`getHandler` returns null for every other predicate, so `ugt`, `uge`, `ule`,
`sgt`, `sge`, `sle` still scalarize. Each is expressible in terms of the four
implemented ones (by swapping operands and/or negating), but that rewrite is
not currently done.

**Compare results are lowered; their consumers are not.** The `icmp` becomes a
carrier sequence, but the `sext`, `select`, or `bitcast` that consumes the
`<K x i1>` result is legalized normally, and for narrow widths that is
expensive. Compare-heavy code therefore keeps a significant cost outside the
pass, which is precisely what `cmp_i4` measures. Extending the pass to
recognize and fold compare-consumer patterns is the obvious next step.

**Loads and stores of narrow vectors are untouched.** Only the operation is
lowered. A `load <32 x i4>` is legalized by scalarizing in both builds, and
because nybbler must then repack for the carrier, a kernel written that way
comes out *slower* under the pass — this is measured, not hypothetical (see
[Why the loads are `<16 x i8>`](#why-the-loads-are-16-x-i8-and-not-32-x-i4)).
Callers must keep memory traffic at byte type and bitcast at the operation to
see the benefit. This is a genuine usability constraint, not just a benchmark
artifact.

**The padding path costs extra on non-byte-multiple widths.** A vector whose
bit width is not a multiple of 8 is widened with zero lanes and narrowed back
afterward, so it pays two `shufflevector`s around a carrier that is partly
padding. A `<3 x i4>` operation, for instance, runs on a 2-byte carrier whose
fourth field is pad and is discarded. The result is correct —
`test/pad_diff.ll` covers all 12 operations × 3 widths on padded shapes — but
the cost per *useful* field is higher than in the byte-multiple case, and no
benchmark kernel measures it because real bit-stream data is byte-aligned.

**Scope is a single function pass.** `NybblerPass` walks instructions in one
function and rewrites `BinaryOperator`s and `ICmpInst`s in place. There is no
interprocedural analysis, no loop-level restructuring, and no attempt to widen
the carrier beyond what the source vector's bit width implies — a `<32 x i4>`
operation gets a 16-byte carrier even on a machine with 32-byte vector
registers.
