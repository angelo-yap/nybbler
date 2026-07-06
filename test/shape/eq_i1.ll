; Shape test: narrow i1 icmp eq lowers onto a <8 x i8> carrier
; (<64 x i1> = 64 bits) with no illegal-type fallback.
; RUN: %opt -load-pass-plugin "%nybbler" -passes=nybbler "%s" -S | %FileCheck "%s"

define <64 x i1> @eq_i1(<64 x i1> %a, <64 x i1> %b) {
; CHECK-LABEL: @eq_i1
; CHECK: bitcast <64 x i1> %a to <8 x i8>
; CHECK: bitcast <64 x i1> %b to <8 x i8>
; CHECK: icmp eq <8 x i8>
; CHECK-NOT: extractelement
  %r = icmp eq <64 x i1> %a, %b
  ret <64 x i1> %r
}
