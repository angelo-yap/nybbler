; Differential test: per-field lshr on the SWAR carrier must match
; LLVM's scalar reference for every input. Driven by tools/diff_runner.py.
;
; The lowering masks each field's shift amount into [0, N-1] so the carrier
; path matches the scalar reference for the in-range cases that the harness
; differentiates.
; RUN: %python "%diff_runner" --opt "%opt" --lli "%lli" --plugin "%nybbler" "%s" | %FileCheck "%s"
; CHECK: ALL PASS

define <64 x i1> @lshr_i1(<64 x i1> %a, <64 x i1> %b) {
  %amt = and <64 x i1> %b, zeroinitializer
  %r = lshr <64 x i1> %a, %amt
  ret <64 x i1> %r
}

define <8 x i2> @lshr_i2(<8 x i2> %a, <8 x i2> %b) {
  %amt = and <8 x i2> %b, splat (i2 1)
  %r = lshr <8 x i2> %a, %amt
  ret <8 x i2> %r
}

define <32 x i4> @lshr_i4(<32 x i4> %a, <32 x i4> %b) {
  %amt = and <32 x i4> %b, splat (i4 3)
  %r = lshr <32 x i4> %a, %amt
  ret <32 x i4> %r
}
