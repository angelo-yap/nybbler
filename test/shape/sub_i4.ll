; Shape test: i4 SWAR sub -- verify the borrow-confining sequence.
; RUN: %opt -load-pass-plugin "%nybbler" -passes=nybbler "%s" -S | %FileCheck "%s"

define <32 x i4> @sub_i4(<32 x i4> %a, <32 x i4> %b) {
; CHECK-LABEL: @sub_i4
; CHECK: bitcast <32 x i4> %a to <16 x i8>
; CHECK: bitcast <32 x i4> %b to <16 x i8>
; CHECK: or  <16 x i8> {{.*}}, <i8 -120, i8 -120, i8 -120, i8 -120, i8 -120, i8 -120, i8 -120, i8 -120, i8 -120, i8 -120, i8 -120, i8 -120, i8 -120, i8 -120, i8 -120, i8 -120>
; CHECK: and <16 x i8> {{.*}}, <i8 119, i8 119, i8 119, i8 119, i8 119, i8 119, i8 119, i8 119, i8 119, i8 119, i8 119, i8 119, i8 119, i8 119, i8 119, i8 119>
; CHECK: sub <16 x i8>
; CHECK: xor <16 x i8>
; CHECK: xor <16 x i8>
; CHECK: bitcast <16 x i8> %{{.*}} to <32 x i4>
; CHECK-NOT: extractelement
  %r = sub <32 x i4> %a, %b
  ret <32 x i4> %r
}
