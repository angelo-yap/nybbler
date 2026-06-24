; Differential test: per-field icmp eq on the SWAR carrier must match
; LLVM's scalar reference for every input. Driven by tools/diff_runner.py.
; The boolean result is sign-extended back to the field width (0 or all-ones
; per field) so the kernel keeps the harness's <K x iN> -> <K x iN> shape.
; RUN: %python "%diff_runner" --opt "%opt" --lli "%lli" --plugin "%nybbler" "%s" | %FileCheck "%s"
; CHECK: ALL PASS

define <64 x i1> @eq_i1(<64 x i1> %a, <64 x i1> %b) {
  ; icmp on i1 fields already returns <64 x i1> -- no extend needed.
  %r = icmp eq <64 x i1> %a, %b
  ret <64 x i1> %r
}

define <8 x i2> @eq_i2(<8 x i2> %a, <8 x i2> %b) {
  %c = icmp eq <8 x i2> %a, %b
  %r = sext <8 x i1> %c to <8 x i2>
  ret <8 x i2> %r
}

define <32 x i4> @eq_i4(<32 x i4> %a, <32 x i4> %b) {
  %c = icmp eq <32 x i4> %a, %b
  %r = sext <32 x i1> %c to <32 x i4>
  ret <32 x i4> %r
}
