; Shape test: narrow i2 icmp slt lowers onto a <2 x i8> carrier
; (<8 x i2> = 16 bits) with no illegal-type fallback.
; RUN: %opt -load-pass-plugin "%nybbler" -passes=nybbler "%s" -S | %FileCheck "%s"
; XFAIL: *
; Pending Slice 2 (arithmetic/shift/compare lowering, see README) -- pass
; does not lower this op yet, so it falls through unchanged.

define <8 x i1> @slt_i2(<8 x i2> %a, <8 x i2> %b) {
; CHECK-LABEL: @slt_i2
; CHECK: bitcast <8 x i2> %a to <2 x i8>
; CHECK: bitcast <8 x i2> %b to <2 x i8>
; CHECK: icmp slt <2 x i8>
; CHECK-NOT: extractelement
  %r = icmp slt <8 x i2> %a, %b
  ret <8 x i1> %r
}
