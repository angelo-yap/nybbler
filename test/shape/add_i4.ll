; Shape test: narrow i4 add lowers onto a <16 x i8> carrier
; (<32 x i4> = 128 bits) with no illegal-type fallback.
; RUN: %opt -load-pass-plugin "%nybbler" -passes=nybbler "%s" -S | %FileCheck "%s"

define <32 x i4> @add_i4(<32 x i4> %a, <32 x i4> %b) {
; CHECK-LABEL: @add_i4
; CHECK: bitcast <32 x i4> %a to <16 x i8>
; CHECK: bitcast <32 x i4> %b to <16 x i8>
; CHECK: add <16 x i8>
; CHECK: and <16 x i8>
; CHECK: bitcast <16 x i8> %{{.*}} to <32 x i4>
; CHECK-NOT: extractelement
  %r = add <32 x i4> %a, %b
  ret <32 x i4> %r
}
