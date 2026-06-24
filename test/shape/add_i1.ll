; Shape test: narrow i1 add lowers onto a <8 x i8> carrier
; (<64 x i1> = 64 bits) with no illegal-type fallback.
; RUN: %opt -load-pass-plugin "%nybbler" -passes=nybbler "%s" -S | %FileCheck "%s"

define <64 x i1> @add_i1(<64 x i1> %a, <64 x i1> %b) {
; CHECK-LABEL: @add_i1
; CHECK: bitcast <64 x i1> %a to <8 x i8>
; CHECK: bitcast <64 x i1> %b to <8 x i8>
; CHECK: add <8 x i8>
; CHECK: and <8 x i8>
; CHECK: bitcast <8 x i8> %{{.*}} to <64 x i1>
; CHECK-NOT: extractelement
  %r = add <64 x i1> %a, %b
  ret <64 x i1> %r
}
