; Differential test: per-field lshr on the SWAR carrier must match
; LLVM's scalar reference for every input. Driven by tools/diff_runner.py.
; RUN: %python "%diff_runner" --opt "%opt" --lli "%lli" --plugin "%nybbler" "%s" | %FileCheck "%s"
; XFAIL: *
; The U2-A scaffolding lowers lshr on the carrier but applies only a placeholder
; boundary mask, so per-field results are not yet correct. Track B (U2-B) drops
; in the amount-dependent masks (clear high s bits per field) and per-field
; variable-shift handling; remove this XFAIL when those land.
; CHECK: ALL PASS

define <64 x i1> @lshr_i1(<64 x i1> %a, <64 x i1> %b) {
  %r = lshr <64 x i1> %a, %b
  ret <64 x i1> %r
}

define <8 x i2> @lshr_i2(<8 x i2> %a, <8 x i2> %b) {
  %r = lshr <8 x i2> %a, %b
  ret <8 x i2> %r
}

define <32 x i4> @lshr_i4(<32 x i4> %a, <32 x i4> %b) {
  %r = lshr <32 x i4> %a, %b
  ret <32 x i4> %r
}
