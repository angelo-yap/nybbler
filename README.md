# nybbler

An out-of-tree LLVM transformation pass that lowers **narrow-field** (`i1`/`i2`/`i4`)
vertical vector operations into legal-width **SWAR** (SIMD-within-a-register)
sequences, so that any backend can lower them directly to SIMD instead of
scalarizing.

SIMD ISAs only expose field-parallel operations at hardware field widths
(8/16/32/64 bits). Narrow fields — central to parallel bit-stream / Parabix-style
processing — are expressible in LLVM IR (`<K x i4>`, `<K x i2>`, `<K x i1>`) but
illegal on real targets, so the default legalizer scalarizes them and discards
the parallelism. nybbler rewrites them into byte-vector carrier ops instead.

## What it lowers

| Class | Operations | `i1` | `i2` | `i4` |
|---|---|:-:|:-:|:-:|
| Bitwise | `and`, `or`, `xor` (and `not`, which LLVM spells as `xor -1`) | ✅ | ✅ | ✅ |
| Arithmetic | `add`, `sub` | ✅ | ✅ | ✅ |
| Logical shift | `shl`, `lshr` | ✅ | ✅ | ✅ |
| Arithmetic shift | `ashr` | ✅ | ✅ | ✅ |
| Comparison | `icmp eq`, `ne`, `ult`, `slt` | ✅ | ✅ | ✅ |

Vectors whose total bit width is not a multiple of 8 are handled by zero-padding
to the next byte boundary and dropping the pad lanes on the way out, so shapes
like `<5 x i1>` lower rather than falling back to the legalizer.

## How it works

Every operation follows the same path, so the pattern lives once in the dispatch
engine and each operation is a single *handler* function keyed by
`(opcode, predicate)`:

1. `total = K * N`; if `total % 8 != 0`, widen to the next byte boundary with
   zero lanes.
2. `bitcast` each operand from `<K x iN>` to the carrier `<total/8 x i8>`.
3. Emit that operation's body on the carrier — the only step that differs
   between operations.
4. `bitcast` back to `<K x iN>`, drop any pad lanes, replace all uses.

Bitwise is the proof of the path: `and`/`or`/`xor` act on each bit independently
and never move a bit across a field boundary, so reinterpreting the same packed
bits as a byte vector and re-emitting the identical opcode is correct by
construction — no masking at all. Everything else is the same carrier with a
body that confines carries, borrows, and shifted-in bits to their own field
using splatted per-field masks.

## Results

Instruction counts from `opt -passes=nybbler | llc -mtriple=x86_64 -mattr=+avx2`,
against the same kernel compiled without the pass (LLVM's scalarizing legalizer
is the baseline). Reproduce with `lit build/test/codegen.test`, or see the full
36-kernel table via `tools/codegen_check.py --report`.

| Operation | `i2` | `i4` |
|---|--:|--:|
| `add` | 63.0× | 31.3× |
| `sub` | 58.2× | 28.9× |
| `shl` | 64.5× | 12.9× |
| `lshr` | 64.4× | 13.0× |
| `ashr` | 29.7× | 6.7× |
| `icmp eq` | 63.4× | 21.1× |
| `icmp ne` | 76.4× | 23.8× |
| `icmp ult` | 47.6× | 19.9× |
| `icmp slt` | 42.3× | 19.2× |

Two categories are absent from the table because they come out 1.0×, and both
are honest results rather than gaps:

- **Bitwise at any width.** LLVM already folds `bitcast → byte op → bitcast` on
  its own, so the pass costs nothing and gains nothing there.
- **Almost everything at `i1`.** A 1-bit field makes most operations degenerate
  — `add` and `sub` are `xor`, `shl` and `lshr` are the identity — so LLVM
  handles them without help. The exception is `icmp eq`, which the legalizer
  does scalarize and the pass brings down 76.6×.

The pass earns its keep on arithmetic, shifts, and comparisons at `i2` and `i4`,
where the legalizer otherwise gives up entirely.

## Try it

```bash
./demo/run.sh
```

Runs the full pipeline on two packed-nibble kernels: prints the narrow-field
source, the carrier sequence nybbler emits, the before/after x86-64 AVX2
assembly with instruction counts, and then builds and runs them on your host,
checking every nibble against a scalar reference in C. Tool paths are
overridable: `OPT=opt LLC=llc CC=clang ./demo/run.sh`.

## Requirements

- LLVM 22 — tools `opt`, `llc`, `lli`, `clang`, `FileCheck`.
  - Ubuntu / WSL2: `llvm-22` from [apt.llvm.org](https://apt.llvm.org)
    (`wget https://apt.llvm.org/llvm.sh && chmod +x llvm.sh && sudo ./llvm.sh 22`),
    which installs to `/usr/lib/llvm-22`.
  - macOS: `brew install llvm@22`, which installs to
    `/opt/homebrew/opt/llvm@22`.
- CMake ≥ 3.20.
- [`lit`](https://pypi.org/project/lit/) for the test suite:
  `pip install --user lit` (or run it from a virtualenv).

## Build

```bash
# Linux
cmake -S . -B build -DLLVM_DIR=/usr/lib/llvm-22/cmake
# macOS
cmake -S . -B build -DLLVM_DIR=/opt/homebrew/opt/llvm@22/lib/cmake/llvm

cmake --build build -j
```

This produces the plugin `build/libNybbler.so`.

## Run on a single file

```bash
opt -load-pass-plugin ./build/libNybbler.so -passes=nybbler in.ll -S
```

## Test

```bash
lit -v build/test/
```

Four layers, all wired into the same `lit` invocation so CI fails if any of them
regresses:

- **Shape** (`test/shape/`) — every operation at every width lowers to the
  carrier form and does not scalarize (`CHECK-NOT: extractelement`).
- **Differential** (`test/diff/`, `test/pad_diff.ll`, `test/shift_overwidth.ll`,
  `test/cmp_mask_*.ll`) — the lowered module and the *unlowered* module are both
  executed by `lli` over structured edge cases plus randomized inputs, and the
  results must be bit-identical. The unlowered module is scalarized by LLVM,
  which makes it a genuine per-field scalar reference.
- **Codegen** (`test/codegen.test`) — every operation at every width goes
  through `llc` for x86-64 AVX2, and must emit vector instructions, contain no
  per-element insert/extract, and never rebuild a field mask at runtime.
- **Coverage** (`test/coverage.test`) — fails if any operation is missing a
  shape or differential test, so a category cannot silently vanish from the
  matrix.

## Limitations

- **`<K x i1>` at a value boundary.** A narrow comparison's natural result type
  has no packed register form — LLVM legalizes it to one boolean per byte lane —
  so any compare whose result must actually be materialized as `<K x i1>` pays a
  bit-by-bit repack that cancels the carrier's benefit. The pass avoids this for
  the common field-mask idiom (`sext` of the compare back to the operand width)
  by handing over the mask it already computed, but a compare feeding, say, a
  `select` still pays it.
- **Narrow vectors as function parameters.** LLVM legalizes a `<32 x i4>`
  argument by promoting each field to its own byte lane, so a narrow vector
  arriving through the ABI is already unpacked and must be repacked before the
  carrier path can use it. Packed narrow fields are expected to come from memory,
  which is the layout a bit-stream buffer already has.
- **Vertical operations only.** No shuffles, reductions, or other horizontal
  operations.
- **No `mul`, `div`, or `rem`**, and among comparisons only `eq`/`ne`/`ult`/`slt`
  — the rest are derivable from these by operand swap and negation, but are not
  currently registered in the dispatch table.
- **Shift amounts are masked into `[0, N-1]` per field.** LLVM defines
  out-of-range shifts as poison, so there is no reference behavior to match;
  the pass picks masking and the differential harness holds it to that.

See [ROADMAP.md](ROADMAP.md) for what is in scope and what comes next.
