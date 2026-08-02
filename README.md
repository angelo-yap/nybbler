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

On the benchmark kernels this is worth **13x** on an `i4` arithmetic chain and
**58x** on an `i2` one, measured against the same backend at the same
optimization level — see [`docs/benchmarks.md`](docs/benchmarks.md).

## What it lowers

| Category | Operations | Widths |
|---|---|---|
| Bitwise | `and`, `or`, `xor` (and `not`, which LLVM spells as `xor -1`) | i1, i2, i4 |
| Arithmetic | `add`, `sub` | i1, i2, i4 |
| Shifts | `shl`, `lshr`, `ashr` | i1, i2, i4 |
| Comparisons | `icmp eq`, `ne`, `ult`, `slt` | i1, i2, i4 |

Every operation goes through one shared **carrier dispatch**: pad to a byte
multiple with zero lanes if needed, bitcast the operands to a `<M x i8>`
carrier, run a per-operation handler, then bitcast and narrow back. Only the
handler differs between operations, so adding one means writing a single
function.

Vectors whose bit width is not a multiple of 8 are **padded**, not skipped.

Anything without a handler — `mul`, `udiv`, the remaining `icmp` predicates —
is left untouched for the default legalizer. The full boundary is in
[Limitations](docs/benchmarks.md#limitations).

## Documentation

- [`docs/lowering.md`](docs/lowering.md) — the per-operation lowerings: carrier
  dispatch, the SWAR add/sub carry and borrow containment, per-field shift
  masking, `ashr` sign handling, the compare lowerings.
- [`docs/correctness.md`](docs/correctness.md) — why the bitwise case is
  correct by construction, how the masked paths keep carries and shifted-in
  bits inside their field, and how the correctness matrix verifies it.
- [`docs/benchmarks.md`](docs/benchmarks.md) — benchmark methodology, measured
  results, and limitations.

## Requirements

- LLVM 22 (this project pins `/usr/lib/llvm-22`; tools `opt-22`, `llc-22`,
  `clang-22`, `lli-22`, `FileCheck-22`). On WSL2 / Ubuntu 24.04 these come from
  the `llvm-22` packages via [apt.llvm.org](https://apt.llvm.org)
  (`wget https://apt.llvm.org/llvm.sh && chmod +x llvm.sh && sudo ./llvm.sh 22`).
- CMake ≥ 3.20.
- [`lit`](https://pypi.org/project/lit/) for the test suite:
  `pip install --user lit` (or run it from a virtualenv).

## Build

```bash
cmake -S . -B build -DLLVM_DIR=/usr/lib/llvm-22/cmake
cmake --build build -j
```

This produces the plugin `build/libNybbler.so`.

## Run on a single file

```bash
opt-22 -load-pass-plugin ./build/libNybbler.so -passes=nybbler in.ll -S
```

Example:

```bash
opt-22 -load-pass-plugin ./build/libNybbler.so -passes=nybbler \
    test/shape/add_i4.ll -S
```

## Test

```bash
lit -v build/test/
```

(Equivalently `llvm-lit-22 build/test/` where that wrapper is installed.) The
suite has four layers:

- `test/shape/` — 36 FileCheck tests asserting the exact carrier sequence per
  operation per width, plus `CHECK-NOT: extractelement` to prove it did not
  scalarize.
- `test/diff/`, `test/pad_diff.ll`, `test/shift_overwidth.ll` — differential
  tests running each kernel both unlowered (scalarized, the ground-truth
  reference) and lowered under `lli`, requiring bit-identical output over
  structured and seeded-random inputs. Reproduce a failure exactly with
  `NYBBLER_DIFF_SEED=<n>`.
- `test/edge_values.ll` — golden hex bytes for boundary inputs.
- `test/coverage.test` — fails if the operation × width matrix has a hole.

## Benchmark

```bash
bash bench/run.sh
```

Builds every kernel in `bench/kernels/` twice — once through `llc` alone
(scalarized baseline) and once through `opt -passes=nybbler` followed by the
identical `llc` line — verifies the two produce byte-identical output, then
prints a timing table. `bash bench/run.sh --check-only` runs just the
correctness gate; that is what CI does, since shared runners are too noisy to
assert a speedup threshold.
