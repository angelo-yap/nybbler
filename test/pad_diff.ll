; Differential coverage for the zero-padding path: non-byte-multiple shapes
; (K*N % 8 != 0) across every lowered op must match LLVM's scalar reference.
; Representative shapes only -- the full padded matrix belongs to U3-C.
;
; Lives at the test root (not diff/) because coverage_check.py treats every
; diff/*.ll filename as an operation requiring per-width shape tests.
; RUN: %python "%diff_runner" --opt "%opt" --lli "%lli" --plugin "%nybbler" "%s" | %FileCheck "%s"
; CHECK: ALL PASS

define <7 x i1> @and_i1_pad(<7 x i1> %a, <7 x i1> %b) {
  %r = and <7 x i1> %a, %b
  ret <7 x i1> %r
}

define <3 x i2> @or_i2_pad(<3 x i2> %a, <3 x i2> %b) {
  %r = or <3 x i2> %a, %b
  ret <3 x i2> %r
}

define <5 x i1> @xor_i1_pad(<5 x i1> %a, <5 x i1> %b) {
  %r = xor <5 x i1> %a, %b
  ret <5 x i1> %r
}

define <13 x i1> @add_i1_pad(<13 x i1> %a, <13 x i1> %b) {
  %r = add <13 x i1> %a, %b
  ret <13 x i1> %r
}

define <5 x i2> @add_i2_pad(<5 x i2> %a, <5 x i2> %b) {
  %r = add <5 x i2> %a, %b
  ret <5 x i2> %r
}

define <3 x i4> @add_i4_pad(<3 x i4> %a, <3 x i4> %b) {
  %r = add <3 x i4> %a, %b
  ret <3 x i4> %r
}

define <7 x i2> @sub_i2_pad(<7 x i2> %a, <7 x i2> %b) {
  %r = sub <7 x i2> %a, %b
  ret <7 x i2> %r
}

define <3 x i4> @sub_i4_pad(<3 x i4> %a, <3 x i4> %b) {
  %r = sub <3 x i4> %a, %b
  ret <3 x i4> %r
}

define <5 x i2> @shl_i2_pad(<5 x i2> %a, <5 x i2> %b) {
  %r = shl <5 x i2> %a, %b
  ret <5 x i2> %r
}

define <3 x i4> @shl_i4_pad(<3 x i4> %a, <3 x i4> %b) {
  %r = shl <3 x i4> %a, %b
  ret <3 x i4> %r
}

define <7 x i2> @lshr_i2_pad(<7 x i2> %a, <7 x i2> %b) {
  %r = lshr <7 x i2> %a, %b
  ret <7 x i2> %r
}

define <3 x i4> @lshr_i4_pad(<3 x i4> %a, <3 x i4> %b) {
  %r = lshr <3 x i4> %a, %b
  ret <3 x i4> %r
}
