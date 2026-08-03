; Differential + shape test for the compare mask fold.
;
; A narrow compare's natural result type <K x i1> has no packed register form:
; the backend legalizes it to one boolean per byte lane, so materializing it
; costs a bit-by-bit repack that cancels out the carrier's whole benefit. When
; the compare only feeds `sext` back to the operand width -- the field-mask
; idiom -- the pass hands over the all-ones/all-zeros mask the handler already
; computed instead of building the <K x i1>, because sext(trunc(m)) == m for a
; uniform field.
;
; That equivalence is the thing under test here: these kernels return the
; sext'd mask, so the harness diffs the folded path against LLVM's scalarized
; reference for the same source. The SHAPE run then asserts the fold actually
; fired (no cmp.trunc, no surviving sext) rather than silently falling back.
;
; i1 is absent by construction: at N=1 the compare result type already *is* the
; field type, so there is no sext to fold and the idiom does not arise.
;
; Lives at the test root, not diff/, because coverage_check.py reads every
; diff/*.ll filename as an operation name needing per-width shape tests.
; RUN: %python "%diff_runner" --opt "%opt" --lli "%lli" --plugin "%nybbler" "%s" | %FileCheck "%s"
; RUN: %opt -load-pass-plugin "%nybbler" -passes=nybbler "%s" -S | %FileCheck --check-prefix=SHAPE "%s"
; CHECK: ALL PASS
; SHAPE-NOT: cmp.trunc
; SHAPE-NOT: = sext

define <32 x i4> @eqmask_i4(<32 x i4> %a, <32 x i4> %b) {
  %c = icmp eq <32 x i4> %a, %b
  %m = sext <32 x i1> %c to <32 x i4>
  ret <32 x i4> %m
}

define <32 x i4> @nemask_i4(<32 x i4> %a, <32 x i4> %b) {
  %c = icmp ne <32 x i4> %a, %b
  %m = sext <32 x i1> %c to <32 x i4>
  ret <32 x i4> %m
}

define <32 x i4> @ultmask_i4(<32 x i4> %a, <32 x i4> %b) {
  %c = icmp ult <32 x i4> %a, %b
  %m = sext <32 x i1> %c to <32 x i4>
  ret <32 x i4> %m
}

define <32 x i4> @sltmask_i4(<32 x i4> %a, <32 x i4> %b) {
  %c = icmp slt <32 x i4> %a, %b
  %m = sext <32 x i1> %c to <32 x i4>
  ret <32 x i4> %m
}

define <64 x i2> @eqmask_i2(<64 x i2> %a, <64 x i2> %b) {
  %c = icmp eq <64 x i2> %a, %b
  %m = sext <64 x i1> %c to <64 x i2>
  ret <64 x i2> %m
}

define <64 x i2> @nemask_i2(<64 x i2> %a, <64 x i2> %b) {
  %c = icmp ne <64 x i2> %a, %b
  %m = sext <64 x i1> %c to <64 x i2>
  ret <64 x i2> %m
}

define <64 x i2> @ultmask_i2(<64 x i2> %a, <64 x i2> %b) {
  %c = icmp ult <64 x i2> %a, %b
  %m = sext <64 x i1> %c to <64 x i2>
  ret <64 x i2> %m
}

define <64 x i2> @sltmask_i2(<64 x i2> %a, <64 x i2> %b) {
  %c = icmp slt <64 x i2> %a, %b
  %m = sext <64 x i1> %c to <64 x i2>
  ret <64 x i2> %m
}
