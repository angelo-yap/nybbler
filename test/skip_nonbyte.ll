; Vectors whose total bit width is not a multiple of 8 are left untouched in
; Slice 1 (no padding) -- the default legalizer handles them.
; RUN: %opt -load-pass-plugin "%nybbler" -passes=nybbler "%s" -S | %FileCheck "%s"

; <3 x i4> = 12 bits, 12 % 8 != 0 -> skip.
define <3 x i4> @and_i4_nonbyte(<3 x i4> %a, <3 x i4> %b) {
; CHECK-LABEL: @and_i4_nonbyte
; CHECK: and <3 x i4> %a, %b
; CHECK-NOT: bitcast
  %r = and <3 x i4> %a, %b
  ret <3 x i4> %r
}

; <5 x i1> = 5 bits, 5 % 8 != 0 -> skip.
define <5 x i1> @xor_i1_nonbyte(<5 x i1> %a, <5 x i1> %b) {
; CHECK-LABEL: @xor_i1_nonbyte
; CHECK: xor <5 x i1> %a, %b
; CHECK-NOT: bitcast
  %r = xor <5 x i1> %a, %b
  ret <5 x i1> %r
}
