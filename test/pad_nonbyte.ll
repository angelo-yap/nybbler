; Vectors whose total bit width is not a multiple of 8 are widened to the next
; byte boundary with zero lanes, lowered on the padded carrier, and narrowed
; back to the original lane count. (Replaces skip_nonbyte.ll, which asserted
; the old behaviour of leaving these to the default legalizer.)
; RUN: %opt -load-pass-plugin "%nybbler" -passes=nybbler "%s" -S | %FileCheck "%s"

; <3 x i4> = 12 bits -> pad to <4 x i4> = 16 bits, carrier <2 x i8>.
define <3 x i4> @and_i4_nonbyte(<3 x i4> %a, <3 x i4> %b) {
; CHECK-LABEL: @and_i4_nonbyte
; CHECK: shufflevector <3 x i4> %a, <3 x i4> zeroinitializer, <4 x i32>
; CHECK: shufflevector <3 x i4> %b, <3 x i4> zeroinitializer, <4 x i32>
; CHECK: bitcast <4 x i4> %{{.*}} to <2 x i8>
; CHECK: and <2 x i8>
; CHECK: bitcast <2 x i8> %{{.*}} to <4 x i4>
; CHECK: shufflevector <4 x i4> %{{.*}}, <3 x i32> <i32 0, i32 1, i32 2>
; CHECK-NOT: and <3 x i4>
  %r = and <3 x i4> %a, %b
  ret <3 x i4> %r
}

; <5 x i1> = 5 bits -> pad to <8 x i1> = 8 bits, carrier <1 x i8>.
define <5 x i1> @xor_i1_nonbyte(<5 x i1> %a, <5 x i1> %b) {
; CHECK-LABEL: @xor_i1_nonbyte
; CHECK: shufflevector <5 x i1> %a, <5 x i1> zeroinitializer, <8 x i32>
; CHECK: shufflevector <5 x i1> %b, <5 x i1> zeroinitializer, <8 x i32>
; CHECK: bitcast <8 x i1> %{{.*}} to <1 x i8>
; CHECK: xor <1 x i8>
; CHECK: bitcast <1 x i8> %{{.*}} to <8 x i1>
; CHECK: shufflevector <8 x i1> %{{.*}}, <5 x i32> <i32 0, i32 1, i32 2, i32 3, i32 4>
; CHECK-NOT: xor <5 x i1>
  %r = xor <5 x i1> %a, %b
  ret <5 x i1> %r
}

; A field-masked op through the same padding wrapper: the SWAR sub body runs
; unchanged on the padded <2 x i8> carrier.
; <5 x i2> = 10 bits -> pad to <8 x i2> = 16 bits, carrier <2 x i8>.
define <5 x i2> @sub_i2_nonbyte(<5 x i2> %a, <5 x i2> %b) {
; CHECK-LABEL: @sub_i2_nonbyte
; CHECK: shufflevector <5 x i2> %a, <5 x i2> zeroinitializer, <8 x i32>
; CHECK: shufflevector <5 x i2> %b, <5 x i2> zeroinitializer, <8 x i32>
; CHECK: bitcast <8 x i2> %{{.*}} to <2 x i8>
; CHECK: sub <2 x i8>
; CHECK: bitcast <2 x i8> %{{.*}} to <8 x i2>
; CHECK: shufflevector <8 x i2> %{{.*}}, <5 x i32> <i32 0, i32 1, i32 2, i32 3, i32 4>
; CHECK-NOT: sub <5 x i2>
  %r = sub <5 x i2> %a, %b
  ret <5 x i2> %r
}
