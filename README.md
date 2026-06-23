# Nybbler

An out-of-tree LLVM transformation pass that lowers **narrow-field** (`i1`/`i2`/`i4`)
vertical vector operations into legal-width **SWAR** (SIMD-within-a-register)
sequences, so that any backend can lower them directly to SIMD instead of
scalarizing.

SIMD ISAs only expose field-parallel operations at hardware field widths
(8/16/32/64 bits). Narrow fields — central to parallel bit-stream / Parabix-style
processing — are expressible in LLVM IR (`<K x i4>`, `<K x i2>`, `<K x i1>`) but
illegal on real targets, so the default legalizer scalarizes them and discards
the parallelism. Nybbler rewrites them into byte-vector carrier ops instead.

## Slice 1 (this revision): bitwise lowering

Lowers the **bitwise** ops `and`, `or`, `xor` (and `not`, which LLVM represents as
`xor` with all-ones). For `%r = <op> <K x iN> %a, %b` with `N ∈ {1, 2, 4}`:

1. `total = K * N`.
2. If `total % 8 != 0`, **skip** (left to the default legalizer; no padding yet).
3. `bitcast` each operand from `<K x iN>` to the carrier `<total/8 x i8>`.
4. Re-emit the same opcode on the carrier operands.
5. `bitcast` the result back to `<K x iN>` and replace all uses.

Bitwise ops act on each bit independently and never move a bit across a field
boundary, so reinterpreting the same packed bits as a byte vector yields
bit-identical per-field results. No masking required — correct by construction.

Arithmetic/shift/compare lowering (with carry/borrow containment), non-byte-multiple
padding, and the runtime differential test harness are deferred to later slices.

## Requirements

- LLVM 16 (this project pins `/usr/lib/llvm-16`; tools `opt-16`, `clang-16`,
  `FileCheck-16`). On WSL2 / Ubuntu 24.04 these come from the `llvm-16` packages.
- CMake ≥ 3.20.
- [`lit`](https://pypi.org/project/lit/) for the test suite:
  `pip install --user lit` (or run it from a virtualenv).

## Build

```bash
cmake -S . -B build -DLLVM_DIR=/usr/lib/llvm-16/cmake
cmake --build build -j
```

This produces the plugin `build/libNybbler.so`.

## Run on a single file

```bash
opt-16 -load-pass-plugin ./build/libNybbler.so -passes=nybbler in.ll -S
```

Example:

```bash
opt-16 -load-pass-plugin ./build/libNybbler.so -passes=nybbler \
    test/bitwise_i4.ll -S
```

## Test

```bash
lit -v build/test/
```

(Equivalently `llvm-lit-16 build/test/` where that wrapper is installed.) The
suite checks each width lowers to the `bitcast → byte-op → bitcast` form, asserts
it did **not** scalarize (`CHECK-NOT: extractelement`), and that non-byte-multiple
vectors are left unchanged.
