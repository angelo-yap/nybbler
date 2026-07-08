; Shape test: i2 SWAR sub -- verify the borrow-confining sequence.
; RUN: %opt -load-pass-plugin "%nybbler" -passes=nybbler "%s" -S | %FileCheck "%s"

define <8 x i2> @sub_i2(<8 x i2> %a, <8 x i2> %b) {
; CHECK-LABEL: @sub_i2
; CHECK: bitcast <8 x i2> %a to <2 x i8>
; CHECK: bitcast <8 x i2> %b to <2 x i8>
; CHECK: or  <2 x i8> %{{.*}}, splat (i8 -86)
; CHECK: and <2 x i8> %{{.*}}, splat (i8 85)
; CHECK: sub <2 x i8>
; CHECK: xor <2 x i8>
; CHECK: xor <2 x i8>
; CHECK: bitcast <2 x i8> %{{.*}} to <8 x i2>
; CHECK-NOT: extractelement
  %r = sub <8 x i2> %a, %b
  ret <8 x i2> %r
}
