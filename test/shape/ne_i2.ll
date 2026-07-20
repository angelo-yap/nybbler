; Shape test: narrow i2 icmp ne lowers onto a <2 x i8> carrier
; (<8 x i2> = 16 bits) with no illegal-type fallback.
; RUN: %opt -load-pass-plugin "%nybbler" -passes=nybbler "%s" -S | %FileCheck "%s"

define <8 x i1> @ne_i2(<8 x i2> %a, <8 x i2> %b) {
; CHECK-LABEL: @ne_i2
; CHECK: bitcast <8 x i2> %a to <2 x i8>
; CHECK: bitcast <8 x i2> %b to <2 x i8>
; CHECK: %eqne.xor = xor <2 x i8>
; CHECK-NOT: extractelement
  %r = icmp ne <8 x i2> %a, %b
  ret <8 x i1> %r
}
