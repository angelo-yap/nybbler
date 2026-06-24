; Shape test: narrow i2 xor lowers onto a <2 x i8> carrier
; (<8 x i2> = 16 bits) with no illegal-type fallback.
; RUN: %opt -load-pass-plugin "%nybbler" -passes=nybbler "%s" -S | %FileCheck "%s"

define <8 x i2> @xor_i2(<8 x i2> %a, <8 x i2> %b) {
; CHECK-LABEL: @xor_i2
; CHECK: bitcast <8 x i2> %a to <2 x i8>
; CHECK: bitcast <8 x i2> %b to <2 x i8>
; CHECK: xor <2 x i8>
; CHECK: bitcast <2 x i8> %{{.*}} to <8 x i2>
; CHECK-NOT: extractelement
  %r = xor <8 x i2> %a, %b
  ret <8 x i2> %r
}
