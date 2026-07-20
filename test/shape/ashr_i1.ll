; Shape test: narrow i1 ashr lowers onto a <8 x i8> carrier
; (<64 x i1> = 64 bits) with no illegal-type fallback.
; N=1 has only one in-range shift amount (0), so lowerAshr special-cases it
; to the identity -- no shift instructions are emitted at all.
; RUN: %opt -load-pass-plugin "%nybbler" -passes=nybbler "%s" -S | %FileCheck "%s"

define <64 x i1> @ashr_i1(<64 x i1> %a, <64 x i1> %b) {
; CHECK-LABEL: @ashr_i1
; CHECK: bitcast <64 x i1> %a to <8 x i8>
; CHECK: bitcast <64 x i1> %b to <8 x i8>
; CHECK: bitcast <8 x i8> %{{.*}} to <64 x i1>
; CHECK-NOT: extractelement
  %r = ashr <64 x i1> %a, %b
  ret <64 x i1> %r
}
