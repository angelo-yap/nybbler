; Differential test: the FULL padded (non-byte-multiple) matrix. Every lowered
; op at each field width {i1,i2,i4} on a shape whose total bit width is not a
; multiple of 8, exercising the pass' zero-pad -> carrier -> narrow path
; (U3-A) end-to-end against LLVM's scalar reference. Companion to the
; byte-multiple kernels in diff/*.ll; together they are the operation x width x
; shape matrix required by U3-C ("byte-multiple and padded ... nothing silently
; skipped").
;
; Lives at the test root (not diff/) because coverage_check.py treats every
; diff/*.ll filename as an operation name requiring per-width shape tests.
;
; Shift amounts are clamped into [0, N-1] per field by the harness (in-range
; case); the at/over-width amount case lives in shift_overwidth.ll.
; RUN: %python "%diff_runner" --opt "%opt" --lli "%lli" --plugin "%nybbler" "%s" | %FileCheck "%s"
; CHECK: ALL PASS


define <5 x i1> @and_i1_pad(<5 x i1> %a, <5 x i1> %b) {
  %r = and <5 x i1> %a, %b
  ret <5 x i1> %r
}

define <3 x i2> @and_i2_pad(<3 x i2> %a, <3 x i2> %b) {
  %r = and <3 x i2> %a, %b
  ret <3 x i2> %r
}

define <3 x i4> @and_i4_pad(<3 x i4> %a, <3 x i4> %b) {
  %r = and <3 x i4> %a, %b
  ret <3 x i4> %r
}

define <12 x i1> @or_i1_pad(<12 x i1> %a, <12 x i1> %b) {
  %r = or <12 x i1> %a, %b
  ret <12 x i1> %r
}

define <5 x i2> @or_i2_pad(<5 x i2> %a, <5 x i2> %b) {
  %r = or <5 x i2> %a, %b
  ret <5 x i2> %r
}

define <5 x i4> @or_i4_pad(<5 x i4> %a, <5 x i4> %b) {
  %r = or <5 x i4> %a, %b
  ret <5 x i4> %r
}

define <7 x i1> @xor_i1_pad(<7 x i1> %a, <7 x i1> %b) {
  %r = xor <7 x i1> %a, %b
  ret <7 x i1> %r
}

define <7 x i2> @xor_i2_pad(<7 x i2> %a, <7 x i2> %b) {
  %r = xor <7 x i2> %a, %b
  ret <7 x i2> %r
}

define <7 x i4> @xor_i4_pad(<7 x i4> %a, <7 x i4> %b) {
  %r = xor <7 x i4> %a, %b
  ret <7 x i4> %r
}

define <13 x i1> @add_i1_pad(<13 x i1> %a, <13 x i1> %b) {
  %r = add <13 x i1> %a, %b
  ret <13 x i1> %r
}

define <3 x i2> @add_i2_pad(<3 x i2> %a, <3 x i2> %b) {
  %r = add <3 x i2> %a, %b
  ret <3 x i2> %r
}

define <3 x i4> @add_i4_pad(<3 x i4> %a, <3 x i4> %b) {
  %r = add <3 x i4> %a, %b
  ret <3 x i4> %r
}

define <11 x i1> @sub_i1_pad(<11 x i1> %a, <11 x i1> %b) {
  %r = sub <11 x i1> %a, %b
  ret <11 x i1> %r
}

define <5 x i2> @sub_i2_pad(<5 x i2> %a, <5 x i2> %b) {
  %r = sub <5 x i2> %a, %b
  ret <5 x i2> %r
}

define <5 x i4> @sub_i4_pad(<5 x i4> %a, <5 x i4> %b) {
  %r = sub <5 x i4> %a, %b
  ret <5 x i4> %r
}

define <3 x i1> @shl_i1_pad(<3 x i1> %a, <3 x i1> %b) {
  %r = shl <3 x i1> %a, %b
  ret <3 x i1> %r
}

define <7 x i2> @shl_i2_pad(<7 x i2> %a, <7 x i2> %b) {
  %r = shl <7 x i2> %a, %b
  ret <7 x i2> %r
}

define <7 x i4> @shl_i4_pad(<7 x i4> %a, <7 x i4> %b) {
  %r = shl <7 x i4> %a, %b
  ret <7 x i4> %r
}

define <9 x i1> @lshr_i1_pad(<9 x i1> %a, <9 x i1> %b) {
  %r = lshr <9 x i1> %a, %b
  ret <9 x i1> %r
}

define <3 x i2> @lshr_i2_pad(<3 x i2> %a, <3 x i2> %b) {
  %r = lshr <3 x i2> %a, %b
  ret <3 x i2> %r
}

define <3 x i4> @lshr_i4_pad(<3 x i4> %a, <3 x i4> %b) {
  %r = lshr <3 x i4> %a, %b
  ret <3 x i4> %r
}

define <6 x i1> @ashr_i1_pad(<6 x i1> %a, <6 x i1> %b) {
  %r = ashr <6 x i1> %a, %b
  ret <6 x i1> %r
}

define <5 x i2> @ashr_i2_pad(<5 x i2> %a, <5 x i2> %b) {
  %r = ashr <5 x i2> %a, %b
  ret <5 x i2> %r
}

define <5 x i4> @ashr_i4_pad(<5 x i4> %a, <5 x i4> %b) {
  %r = ashr <5 x i4> %a, %b
  ret <5 x i4> %r
}

define <10 x i1> @eq_i1_pad(<10 x i1> %a, <10 x i1> %b) {
  %r = icmp eq <10 x i1> %a, %b
  ret <10 x i1> %r
}

define <7 x i2> @eq_i2_pad(<7 x i2> %a, <7 x i2> %b) {
  %c = icmp eq <7 x i2> %a, %b
  %r = sext <7 x i1> %c to <7 x i2>
  ret <7 x i2> %r
}

define <7 x i4> @eq_i4_pad(<7 x i4> %a, <7 x i4> %b) {
  %c = icmp eq <7 x i4> %a, %b
  %r = sext <7 x i1> %c to <7 x i4>
  ret <7 x i4> %r
}

define <14 x i1> @ne_i1_pad(<14 x i1> %a, <14 x i1> %b) {
  %r = icmp ne <14 x i1> %a, %b
  ret <14 x i1> %r
}

define <3 x i2> @ne_i2_pad(<3 x i2> %a, <3 x i2> %b) {
  %c = icmp ne <3 x i2> %a, %b
  %r = sext <3 x i1> %c to <3 x i2>
  ret <3 x i2> %r
}

define <5 x i4> @ne_i4_pad(<5 x i4> %a, <5 x i4> %b) {
  %c = icmp ne <5 x i4> %a, %b
  %r = sext <5 x i1> %c to <5 x i4>
  ret <5 x i4> %r
}

define <15 x i1> @ult_i1_pad(<15 x i1> %a, <15 x i1> %b) {
  %r = icmp ult <15 x i1> %a, %b
  ret <15 x i1> %r
}

define <5 x i2> @ult_i2_pad(<5 x i2> %a, <5 x i2> %b) {
  %c = icmp ult <5 x i2> %a, %b
  %r = sext <5 x i1> %c to <5 x i2>
  ret <5 x i2> %r
}

define <3 x i4> @ult_i4_pad(<3 x i4> %a, <3 x i4> %b) {
  %c = icmp ult <3 x i4> %a, %b
  %r = sext <3 x i1> %c to <3 x i4>
  ret <3 x i4> %r
}

define <5 x i1> @slt_i1_pad(<5 x i1> %a, <5 x i1> %b) {
  %r = icmp slt <5 x i1> %a, %b
  ret <5 x i1> %r
}

define <7 x i2> @slt_i2_pad(<7 x i2> %a, <7 x i2> %b) {
  %c = icmp slt <7 x i2> %a, %b
  %r = sext <7 x i1> %c to <7 x i2>
  ret <7 x i2> %r
}

define <7 x i4> @slt_i4_pad(<7 x i4> %a, <7 x i4> %b) {
  %c = icmp slt <7 x i4> %a, %b
  %r = sext <7 x i1> %c to <7 x i4>
  ret <7 x i4> %r
}
