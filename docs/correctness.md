# Correctness

Why the lowerings in [`lowering.md`](lowering.md) compute the same thing the
scalarized reference does, and how that is checked rather than merely asserted.

The whole problem has one shape. A carrier byte packs `8/N` independent
fields; the hardware operation we emit does not know they are independent. So
for each lowering there is exactly one question: **can any effect of field *i*
reach field *j ≠ i*?** For bitwise the answer is no by the nature of the
operation. For everything else the answer is no because of a mask, and the
sections below say which mask and why it is sufficient.

## Bitwise is correct by construction

`and`, `or`, and `xor` compute each output bit from the two input bits at the
same position, and from nothing else. No bit of the result depends on any
other bit position.

A `bitcast` from `<K x iN>` to `<K*N/8 x i8>` is a pure reinterpretation: it
moves no bits, it only changes how they are typed. So bit *p* of the carrier
is bit *p* of the original packed vector, for every *p*.

Composing those two facts: bit *p* of `and` on the carrier is the `and` of bit
*p* of each operand, which is bit *p* of `and` on the narrow vector. That holds
for every bit position, so the results are bit-identical, and therefore
field-identical. Field boundaries never enter the argument, which is why no
mask is needed and why the handler is a single instruction.

This is also the foundation for everything else: every masked lowering below
is built out of bitwise operations plus carrier `add`/`sub`/shifts, and the
bitwise parts are exact by this argument.

## The masked paths keep effects inside their field

### `add`

The only way a carrier `add` corrupts a neighbour is a carry out of bit `N-1`
of a field into bit `N` — which is bit 0 of the next field.

Masking both operands with `L` clears each field's top bit, so each field's
operand value is at most `2^(N-1) - 1`. Their sum is at most `2^N - 2`, which
is representable in `N` bits. **No carry out of the field is possible**, so
the carrier `add` behaves as `8/N` independent `N`-bit adds.

What is left is that the top bit of each field was zeroed on both inputs. The
true top bit of a sum is `a_top ^ b_top ^ carry_in`, where `carry_in` is the
carry into bit `N-1` — and that carry is exactly what the masked add already
propagated internally, so it is present in the masked sum's top bit. XORing in
`(a ^ b) & H` supplies the remaining `a_top ^ b_top`. XOR is bitwise, so by the
argument above it cannot generate a carry and cannot disturb the containment
just established.

### `sub`

Symmetric. A carrier `sub` corrupts a neighbour only via a borrow out of a
field.

`a | H` forces each field's minuend to have its top bit set, so its value is
at least `2^(N-1)`. `b & L` clears the subtrahend's top bit, so its value is at
most `2^(N-1) - 1`. The minuend therefore strictly exceeds the subtrahend in
every field, the difference is non-negative, and **no borrow leaves the
field**.

The two trailing XORs undo the forcing: `^ H` removes the bit that was
injected into the minuend, and `^ ((a ^ b) & H)` restores the true top bit by
the same reasoning as add. Both are bitwise and so carry-free.

### `shl`, `lshr`, `ashr`

These are the cases where the carrier operation genuinely *does* cross field
boundaries — a carrier `shl` by `S` moves the top `S` bits of every field into
the bottom of its neighbour. Containment here is not a property of the shift;
it is restored explicitly afterward.

Three separate things have to stay in their field, and each is handled:

1. **The shifted data.** After the carrier shift by `S`, the bits in each
   field are of two kinds: those that came from this field (a genuine in-field
   shift by `S`) and those that arrived from the neighbour. For `shl` the
   former occupy exactly the top `N-S` bit positions of the field, for `lshr`
   the bottom `N-S`. The boundary mask `BPat` is precisely that set of
   positions, so ANDing with it keeps every legitimate bit and drops every
   imported one. When `S >= N` the set is empty, `BPat` is zero, and the field
   is cleared — which is the right answer for a shift by at least the field
   width.

2. **The selector.** The per-field selector must be all-ones or all-zeros
   across the field, built from a single bit. Deriving it as `0 - bit` would
   be a carrier subtraction whose borrows would bleed across fields — exactly
   the failure mode being avoided. It is instead built by ORing the bit with
   itself shifted left by 1, 2, …, `< N`. Each such shift moves a bit from
   position 0 to a position `< N`, i.e. within the same field, so the selector
   is constructed entirely by in-field movement and never needs masking.

3. **The amount extraction.** Bringing amount bit `J` down to bit 0 uses a
   carrier `lshr` by `J`, which can pull a neighbour's bits in; the subsequent
   `& LowBit` keeps only bit 0 of each field, discarding them.

The final blend `(taken & sel) | (current & ~sel)` is bitwise, hence
field-local.

For `ashr` the same three arguments apply unchanged, plus the sign fill: the
sign bit is isolated with `& H` (field-local), smeared downward by in-field
`lshr`s, and ORed in only at the positions `~BPat` that the shift vacated —
which is the complement, within the field, of the positions `BPat` kept. Every
bit of the field is therefore written by exactly one of the two, and nothing
outside the field is touched.

### Comparisons

`eq`/`ne` reduce `a ^ b` to a single "any bit set" bit per field. The reduction
folds with `lshr`, which crosses boundaries, so each shifted value is masked
with `(1 << (N-S)) - 1` before being folded in — the same boundary-mask
reasoning as `lshr` itself. The smear back up uses in-field `shl`s only, as in
the shift selector.

`ult` reuses `sub`'s absorber, so the subtraction it performs is contained by
the argument already given. Everything after it — the `(~a & b) | (~(a ^ b) &
~raw)` expression and the `& H` — is bitwise, and the final smear downward
uses in-field `lshr`s.

`slt` is `ult` after an `^ H` on both operands. XOR is bitwise, and flipping
the sign bit maps signed order onto unsigned order within each field
independently.

### The padded path

Non-byte-multiple vectors are widened with zero lanes before any of the above
runs. Those pad lanes are ordinary fields as far as the handlers are
concerned, and every argument above shows that a field's computation cannot
reach another field — so a pad lane cannot influence a real lane, whatever it
computes. The narrowing `shufflevector` afterward discards them. The masks
need no adjustment because they are per-byte splats and so already cover the
pad fields.

### Degenerate widths

Two lowerings are not valid at `N=1` and are not used there:

- `add`/`sub` would have `L = 0`, discarding both operands. At `N=1`
  arithmetic is mod 2, i.e. `xor`, and `tryLower` remaps them before dispatch.
- `ult`/`slt` would have `H` all-ones and `L` all-zeros, which discards `a` and
  computes a plain `xor`. `tryLower` emits `~a & b` and `a & ~b` inline
  instead.

`eq`/`ne` and the shifts degenerate *correctly* at `N=1` (the helper loops
simply do not execute), so they need no special case — but the differential
suite covers `i1` for every operation regardless, rather than relying on that
reasoning.

## How this is verified

The argument above is not the evidence. The evidence is the correctness matrix
landed in `e9e14c7`, which checks every operation at every width by several
independent means.

**Differential testing against a genuine scalar reference.**
`tools/diff_runner.py` builds one module containing the kernel, runs it two
ways under `lli` — unlowered (where LLVM's legalizer scalarizes the narrow op,
giving ground-truth per-field semantics) and after `opt -passes=nybbler` — and
requires the printed output bytes to be identical. The harness contains no
per-operation knowledge at all, so it cannot be tuned to agree with a wrong
lowering. Inputs are the cross product of the structured fills
`[0x00, 0xFF, 0xAA, 0x55, 0x80, 0x7F]` — covering all-zero, all-one,
alternating, and sign-boundary field patterns at every width — plus 100 seeded
random pairs. The seed is printed and overridable via `NYBBLER_DIFF_SEED` so
any CI failure reproduces exactly.

- `test/diff/*.ll` — all 12 operations, byte-multiple shapes.
- `test/pad_diff.ll` — the same 12 operations × 3 widths on non-byte-multiple
  shapes (`<3 x i4>`, `<7 x i2>`, `<13 x i1>`), exercising the padding path.
- `test/shift_overwidth.ll` — 12 kernels that mask the amount in IR so the
  reference is defined for raw out-of-range amounts, then fed the *unclamped*
  amount range so amounts `>= N` actually flow through.

**Shape testing.** `test/shape/<op>_i{1,2,4}.ll` — 36 files — FileCheck the
exact carrier instruction sequence and assert `CHECK-NOT: extractelement`,
i.e. that the lowering fired and the operation did not fall back to
scalarization. Differential testing alone cannot catch a "correct but
scalarized" regression; this is what does.

**Golden boundary values.** `test/edge_values.ll` lowers with the pass, runs
under `lli`, and FileChecks literal hex bytes for all-zeros, all-ones, and
sign-boundary inputs across all three widths — a fixed expected answer rather
than agreement with a reference.

**Matrix completeness.** `test/coverage.test` runs `tools/coverage_check.py`,
which fails if any operation lacks a shape test at each of `i1`/`i2`/`i4` or
lacks a differential kernel. This is what keeps the matrix from silently
developing a hole when an operation is added.

**Benchmark checksums.** `test/bench_checksum.test` additionally requires the
baseline and lowered builds of every benchmark kernel to produce byte-identical
output natively — a check on the *native* code path, which the `lli`-based
suite does not cover. See [`benchmarks.md`](benchmarks.md).

Run it all with `lit -v build/test/`; run it against a different seed with
`NYBBLER_DIFF_SEED=1234 lit -v build/test/`.
