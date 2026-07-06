; Shape test: narrow i1 icmp ult lowers onto a <8 x i8> carrier
; (<64 x i1> = 64 bits) with no illegal-type fallback.
; RUN: %opt -load-pass-plugin "%nybbler" -passes=nybbler "%s" -S | %FileCheck "%s"
; XFAIL: *
; Pending Slice 2 (arithmetic/shift/compare lowering, see README) -- pass
; does not lower this op yet, so it falls through unchanged.

define <64 x i1> @ult_i1(<64 x i1> %a, <64 x i1> %b) {
; CHECK-LABEL: @ult_i1
; CHECK: bitcast <64 x i1> %a to <8 x i8>
; CHECK: bitcast <64 x i1> %b to <8 x i8>
; CHECK: icmp ult <8 x i8>
; CHECK-NOT: extractelement
  %r = icmp ult <64 x i1> %a, %b
  ret <64 x i1> %r
}
