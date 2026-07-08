; Shape test: narrow i4 icmp ult lowers onto a <16 x i8> carrier
; (<32 x i4> = 128 bits) with no illegal-type fallback.
; RUN: %opt -load-pass-plugin "%nybbler" -passes=nybbler "%s" -S | %FileCheck "%s"
; XFAIL: *
; Pending Slice 2 (arithmetic/shift/compare lowering, see README) -- pass
; does not lower this op yet, so it falls through unchanged.

define <32 x i1> @ult_i4(<32 x i4> %a, <32 x i4> %b) {
; CHECK-LABEL: @ult_i4
; CHECK: bitcast <32 x i4> %a to <16 x i8>
; CHECK: bitcast <32 x i4> %b to <16 x i8>
; CHECK: icmp ult <16 x i8>
; CHECK-NOT: extractelement
  %r = icmp ult <32 x i4> %a, %b
  ret <32 x i1> %r
}
