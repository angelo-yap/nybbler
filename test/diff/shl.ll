; Differential test: per-field shl on the SWAR carrier must match
; LLVM's scalar reference for every input. Driven by tools/diff_runner.py.
; RUN: %python "%diff_runner" --opt "%opt" --lli "%lli" --plugin "%nybbler" "%s" | %FileCheck "%s"
; CHECK: ALL PASS

define <64 x i1> @shl_i1(<64 x i1> %a, <64 x i1> %b) {
  %r = shl <64 x i1> %a, %b
  ret <64 x i1> %r
}

define <8 x i2> @shl_i2(<8 x i2> %a, <8 x i2> %b) {
  %r = shl <8 x i2> %a, %b
  ret <8 x i2> %r
}

define <32 x i4> @shl_i4(<32 x i4> %a, <32 x i4> %b) {
  %r = shl <32 x i4> %a, %b
  ret <32 x i4> %r
}
