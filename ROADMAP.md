# Roadmap

Where nybbler is, what it deliberately does not do, and what would come next.

## Shipped

The pass lowers the full vertical operation set at every narrow width — bitwise,
`add`/`sub`, `shl`/`lshr`/`ashr`, and `icmp eq`/`ne`/`ult`/`slt` at `i1`, `i2`,
and `i4` — including non-byte-multiple shapes via zero padding. See the table in
[README.md](README.md).

Correctness is held by a four-layer suite (shape, differential, codegen,
coverage) that runs as one `lit` invocation, and the codegen layer holds the
performance claim by failing if any operation stops reaching SIMD.

Development ran as four updates on three parallel tracks:

| Update | Delivered |
|---|---|
| 1 | Out-of-tree pass skeleton, carrier dispatch refactor, SWAR `add`, runtime differential harness |
| 2 | SWAR `sub`, per-field `shl`/`lshr`, structured test matrix, CI |
| 3 | `ashr` and comparisons, non-byte-multiple padding, full correctness matrix |
| 4 | Backend codegen verification, compare mask fold, benchmarks, report |

## Non-goals

These are out of scope by choice, not oversight:

- **Horizontal operations.** Shuffles, reductions, and cross-lane movement.
  The carrier abstraction is built on the fact that a vertical operation never
  moves a bit across a field boundary; horizontal operations are a different
  problem needing a different lowering.
- **`mul`, `div`, `rem`.** SWAR multiply is possible but needs partial-product
  accumulation with per-field containment that has little in common with the
  existing handlers. Division has no practical SWAR form at these widths.
- **Widths other than `i1`/`i2`/`i4`.** `i8` and above are already legal.
  Non-power-of-two field widths (`i3`, `i5`) do not tile a byte evenly and would
  need a different carrier decomposition.
- **Autovectorization.** The pass lowers narrow-field vector IR that is already
  there; it does not find scalar loops and turn them into it.

## Next

Roughly in order of value per unit of work:

1. **Remaining comparison predicates** — `ugt`/`uge`/`ule`/`sgt`/`sge`/`sle` are
   each an operand swap or a negation away from the four already implemented.
   Mostly dispatch-table entries plus tests.
2. **Widen the compare mask fold.** The fold currently fires only for `sext`
   users. A `select` on a narrow compare is just as common and could take the
   same mask via blend, which would close the largest remaining case where a
   lowered compare still repacks through `<K x i1>`.
3. **Carrier width above 128 bits.** The carrier is currently whatever the
   source vector's bit width implies. Splitting or widening to the target's
   preferred vector width (256 for AVX2, 512 for AVX-512) would let a single
   narrow operation fill the widest available register.
4. **Cost-model awareness.** Bitwise operations come out 1.0× because LLVM
   already handles them; the pass could skip them entirely rather than emitting
   IR that instcombine has to fold back.
5. **Targets beyond x86-64.** Nothing in the lowering is x86-specific — it emits
   generic byte-vector IR — but only x86-64 AVX2 has been measured. AArch64 NEON
   is the obvious next data point, and the demo already builds and runs there.
