; Shape test: narrow i1 sub lowers onto a <8 x i8> carrier
; (<64 x i1> = 64 bits) with no illegal-type fallback.
; RUN: %opt -load-pass-plugin "%nybbler" -passes=nybbler "%s" -S | %FileCheck "%s"
; XFAIL: *
; Pending Slice 2 (arithmetic/shift/compare lowering, see README) -- pass
; does not lower this op yet, so it falls through unchanged.

define <64 x i1> @sub_i1(<64 x i1> %a, <64 x i1> %b) {
; CHECK-LABEL: @sub_i1
; CHECK: bitcast <64 x i1> %a to <8 x i8>
; CHECK: bitcast <64 x i1> %b to <8 x i8>
; CHECK: sub <8 x i8>
; CHECK: or <8 x i8>
; CHECK: bitcast <8 x i8> %{{.*}} to <64 x i1>
; CHECK-NOT: extractelement
  %r = sub <64 x i1> %a, %b
  ret <64 x i1> %r
}
