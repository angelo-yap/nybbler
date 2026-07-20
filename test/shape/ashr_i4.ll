; Shape test: narrow i4 ashr lowers onto a <16 x i8> carrier
; (<32 x i4> = 128 bits) with no illegal-type fallback.
; RUN: %opt -load-pass-plugin "%nybbler" -passes=nybbler "%s" -S | %FileCheck "%s"

define <32 x i4> @ashr_i4(<32 x i4> %a, <32 x i4> %b) {
; CHECK-LABEL: @ashr_i4
; CHECK: bitcast <32 x i4> %a to <16 x i8>
; CHECK: bitcast <32 x i4> %b to <16 x i8>
; CHECK: %ashr.sign = and <16 x i8>
; CHECK: %ashr.signbcast = or <16 x i8>
; CHECK: %ashr.hasstep = and <16 x i8>
; CHECK: bitcast <16 x i8> %{{.*}} to <32 x i4>
; CHECK-NOT: extractelement
  %r = ashr <32 x i4> %a, %b
  ret <32 x i4> %r
}
