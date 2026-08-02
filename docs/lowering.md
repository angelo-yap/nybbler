# Per-operation lowerings

How each narrow-field operation is rewritten onto a byte-vector carrier.
Source of truth is [`lib/Nybbler.cpp`](../lib/Nybbler.cpp); this document
explains *why* each formula is the formula. The correctness argument for these
lowerings is in [`correctness.md`](correctness.md); measured cost is in
[`benchmarks.md`](benchmarks.md).

Throughout, `N` is the field width (1, 2, or 4), `K` the lane count, and a
**carrier byte** packs `8/N` fields.

## The carrier dispatch

`lowerNarrowOp` holds everything that is the same for every operation, so each
operation is one small handler function rather than a full rewrite.

For `%r = <op> <K x iN> %a, %b`:

1. **Pad.** If `K` is not a multiple of `8/N`, widen both operands to `K'`
   lanes by appending zero lanes via `shufflevector` against a zero vector, so
   that `K' * N` is a whole number of bytes.
2. **Bitcast in.** Reinterpret each padded operand from `<K' x iN>` to the
   carrier type `<K'*N/8 x i8>`. This is a pure reinterpretation — no bits
   move.
3. **Run the handler.** One `CarrierHandler`, selected by `(opcode,
   predicate)` in `getHandler`, emits the operation's body on byte vectors.
4. **Bitcast back**, and for `icmp` additionally `trunc` each `iN` lane to
   `i1`.
5. **Narrow.** Drop the pad lanes with a second `shufflevector`, then
   `replaceAllUsesWith` and erase the original.

Only step 3 differs between operations. That is the entire architectural
claim: adding an operation means writing one handler and registering it in
`getHandler` — nothing else changes.

Two details worth stating explicitly:

- **Pad lanes are inert.** Every handler's masks are per-byte splats, so they
  already cover pad fields, and every handler confines its effects within a
  field (that is exactly what the rest of this document establishes). A pad
  lane's value therefore cannot reach a real lane, and whatever junk the pad
  fields compute is discarded by the narrowing shuffle.
- **The compare `trunc` needs no masking.** Every compare handler produces
  all-ones or all-zeros per field, so truncating an `iN` lane to `i1` reads
  the low bit and gets the right boolean for free.

## `splatFieldPattern` — the one mask primitive

Every field-aware handler builds its masks through a single helper. Given a
bit pattern for *one field*, it replicates that pattern across all `8/N`
fields of a byte and splats the byte across every carrier lane:

| `N` | pattern | byte |
|-----|---------|------|
| 4 | `0b1000` | `0x88` |
| 4 | `0b0111` | `0x77` |
| 2 | `0b10`   | `0xAA` |
| 2 | `0b01`   | `0x55` |
| 1 | `0b1`    | `0xFF` |

Two masks recur often enough to name:

- **`H`** — the top bit of each field: `splatFieldPattern(N, 1 << (N-1))`.
- **`L`** — every bit *below* the top: `splatFieldPattern(N, (1 << (N-1)) - 1)`.

At `N=4` those are `0x88` and `0x77`; you can see both broadcast into the
loop preamble of the generated code in `benchmarks.md`.

A companion helper, `splatAmount`, splats a plain per-byte-lane scalar — used
for the carrier-level shift amounts inside the barrel-shift loops, which are
compile-time constants, not per-field data.

## Bitwise — `and`, `or`, `xor`

```
result = <same opcode> on the carrier bytes
```

That is the whole handler: one `CreateBinOp`, no masks. Bitwise operations act
on each bit independently and never move a bit across a field boundary, so
reinterpreting the packed bits as bytes and re-emitting the identical opcode
is bit-identical per field. `not` arrives as `xor %a, -1` and rides the same
path.

This is the case that is correct by construction; everything below has to work
for its correctness.

## `add` — SWAR carry containment

```
result = ((a & L) + (b & L)) ^ ((a ^ b) & H)
```

A carrier `add` on packed fields is wrong only because a carry out of field
*i* lands in field *i+1*. So: clear each field's top bit on both operands
before adding. Each field's operands are then at most `2^(N-1) - 1`, their sum
at most `2^N - 2`, which still fits in `N` bits — no carry can escape.

That leaves the top bit of each field unaccounted for. The top bit of a sum is
`a_top ^ b_top ^ carry_in`, and `carry_in` is precisely the carry the masked
add just produced into bit `N-1`, which is already sitting in `Sum`. So xoring
in `(a ^ b) & H` completes it. `xor` cannot itself generate a carry, so the
containment is not disturbed.

`i1` add is arithmetic mod 2, i.e. plain `xor`; `tryLower` remaps it before
dispatch rather than running this formula at a width where `L` is zero.

## `sub` — SWAR borrow absorption

```
result = ((a | H) - (b & L)) ^ H ^ ((a ^ b) & H)
```

The dual of add. A carrier `sub` is wrong only because a borrow out of field
*i* is taken from field *i+1*. Forcing `a`'s top bit to 1 and clearing `b`'s
guarantees the field minuend strictly exceeds the field subtrahend, so the
borrow is absorbed inside the field and never propagates. The trailing
`^ H ^ ((a ^ b) & H)` undoes the forcing and restores the true top bit, by the
same reasoning as add.

Worked `i4` trace — low field `6 - 4 = 2`, high field `5 - 3 = 2`, so
`a = 0x56`, `b = 0x34`, `H = 0x88`:

```
a | H          = 0xDE
b & L          = 0x34
0xDE - 0x34    = 0xAA
(a ^ b) & H    = 0x00
0xAA ^ 0x88 ^ 0x00 = 0x22   ✓
```

`i1` sub is also `xor` and is remapped before dispatch.

## `shl` / `lshr` — per-field masked barrel shift

Each field carries its **own** shift amount, so a single carrier shift will
not do. The handler runs a bit-serial barrel shift over each bit `J` of the
amount, step `S = 2^J`:

1. **Isolate amount bit `J`.** An `lshr` of the amount vector by `J` brings
   that bit to each field's bit 0; `& LowBit` drops any bleed from the
   neighbouring field.
2. **Smear it to a full-field selector.** The obvious way to turn a 0/1 bit
   into a 0x0/all-ones mask is `0 - bit`, but on a packed carrier that
   subtraction's borrows would bleed across fields. Instead the bit is smeared
   upward with in-field `shl`s: shifting a field's bit 0 left by less than `N`
   keeps it inside the field, so the selector is built without ever crossing a
   boundary and needs no masking of its own.
3. **Shift and confine.** Apply the carrier shift by `S` — which *does* move
   bits across boundaries — then `AND` with a boundary mask `BPat` that keeps
   exactly the bits that legitimately survive an in-field shift by `S`:
   `shl` keeps the top `N-S` bits of each field, `lshr` the bottom `N-S`.
   Anything that arrived from a neighbour falls outside that mask and is
   dropped. When `S >= N`, `BPat` is zero and the field is correctly cleared.
4. **Blend per field:** `(shifted & sel) | (current & ~sel)`.

**Amount handling.** The amount is masked into `[0, N-1]` up front. A shift
count `>= N` is poison per the LangRef, so there is no reference value to
match; masking is the defined behaviour the project commits to, and
`test/shift_overwidth.ll` asserts it end-to-end against kernels that perform
the same masking in IR.

`N=1` is identity: the only in-range amount is 0.

## `ashr` — sign fill

Same skeleton as `lshr`, with two changes.

**The vacated bits are filled with the field's sign.** The sign bit is
extracted once up front (`a & H`) and smeared *downward* across the whole
field. At each step, after confining the shifted-in bits to the surviving
low `N-S`, the sign mask restricted to the vacated top `S` bits
(`FillPat = ~BPat`) is ORed in. When `S >= N` nothing survives and the field
becomes pure sign fill.

**The amount is deliberately left unmasked.** This is the one place `ashr`
diverges from `shl`/`lshr`, and it is intentional: the barrel loop walks every
bit of the full field-width amount and therefore accumulates the amount's
exact integer value, so an over-width amount naturally saturates to complete
sign fill — which is what the scalar reference produces. Masking down to
`[0, N-1]` would instead *wrap* an out-of-range amount back into range and
disagree with the reference.

`N=1` is identity, matching the `shl`/`lshr` special case.

## `icmp eq` / `icmp ne`

```
ne  = reduceAndBroadcastField(a ^ b)
eq  = ~ne
```

`a ^ b` is zero in a field exactly when the fields are equal, so the compare
reduces to "is any bit of this field set?". `reduceAndBroadcastField` answers
that in two phases:

- **OR-reduce toward bit 0.** Fold with `lshr` by 1, 2, 4, …, masking each
  shifted value with `splatFieldPattern(N, (1 << (N-S)) - 1)` first — the same
  boundary-mask idea as `lshr`, because the fold's shift would otherwise pull
  in the neighbour's bits.
- **Smear bit 0 back up** across the field with in-field `shl`s, giving
  all-ones or all-zeros.

Both loops are empty at `N=1`, so the helper is the identity there and `eq`/`ne`
degenerate correctly to `xnor`/`xor`.

## `icmp ult` — compare by subtraction

Reuses `sub`'s borrow absorber: with `a`'s top bit forced to 1 and `b`'s
cleared, the subtraction stays inside each field. The result's per-field top
bit is then read through the standard SWAR less-than expression

```
t   = (~a & b) | (~(a ^ b) & ~raw)
top = t & H
```

which is 1 exactly when `a <u b` in that field, and smeared downward to a
full-field all-ones/all-zeros mask.

## `icmp slt`

Flip the sign bit of both operands (`^ H`) and delegate to `ult`. Flipping the
sign bit maps the signed order onto the unsigned order — the standard
signed-to-unsigned bias trick, applied per field.

## `i1` compare special cases

`eq`/`ne` degenerate correctly at `N=1`, but `ult`/`slt` do **not**: their
formula depends on `H` and `L` being distinct, and at `N=1` they collapse to
all-ones and all-zeros, discarding `a` entirely. `tryLower` therefore handles
these two inline, before dispatch, with single-bit logic:

- `ult(a, b) = ~a & b` — `0 <u 1` is the only true case.
- `slt(a, b) = a & ~b` — bit 1 represents `-1`, the only negative `i1`, so
  `-1 <s 0` is the only true case.

## What is dispatched, and what is not

`NybblerPass::run` collects `BinaryOperator`s whose result type is a narrow
field vector, and `ICmpInst`s whose *operand* type is one, then lowers each.
`getHandler` returns null — leaving the instruction to the default legalizer —
for any opcode without a handler, and for any `icmp` predicate outside
`eq`/`ne`/`ult`/`slt`. See the Limitations section of
[`benchmarks.md`](benchmarks.md#limitations) for the full boundary.
