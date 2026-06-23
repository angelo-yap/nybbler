; Narrow i4 bitwise ops lower to a <16 x i8> carrier (<32 x i4> = 128 bits).
; RUN: %opt -load-pass-plugin "%nybbler" -passes=nybbler "%s" -S | %FileCheck "%s"

define <32 x i4> @and_i4(<32 x i4> %a, <32 x i4> %b) {
; CHECK-LABEL: @and_i4
; CHECK: bitcast <32 x i4> %a to <16 x i8>
; CHECK: bitcast <32 x i4> %b to <16 x i8>
; CHECK: and <16 x i8>
; CHECK: bitcast <16 x i8> %{{.*}} to <32 x i4>
; CHECK-NOT: extractelement
  %r = and <32 x i4> %a, %b
  ret <32 x i4> %r
}

define <32 x i4> @or_i4(<32 x i4> %a, <32 x i4> %b) {
; CHECK-LABEL: @or_i4
; CHECK: bitcast <32 x i4> %a to <16 x i8>
; CHECK: bitcast <32 x i4> %b to <16 x i8>
; CHECK: or <16 x i8>
; CHECK: bitcast <16 x i8> %{{.*}} to <32 x i4>
; CHECK-NOT: extractelement
  %r = or <32 x i4> %a, %b
  ret <32 x i4> %r
}

define <32 x i4> @xor_i4(<32 x i4> %a, <32 x i4> %b) {
; CHECK-LABEL: @xor_i4
; CHECK: bitcast <32 x i4> %a to <16 x i8>
; CHECK: bitcast <32 x i4> %b to <16 x i8>
; CHECK: xor <16 x i8>
; CHECK: bitcast <16 x i8> %{{.*}} to <32 x i4>
; CHECK-NOT: extractelement
  %r = xor <32 x i4> %a, %b
  ret <32 x i4> %r
}

; `not` is xor with all-ones; it lowers through the same path.
define <32 x i4> @not_i4(<32 x i4> %a) {
; CHECK-LABEL: @not_i4
; CHECK: bitcast <32 x i4> %a to <16 x i8>
; CHECK: xor <16 x i8>
; CHECK: bitcast <16 x i8> %{{.*}} to <32 x i4>
; CHECK-NOT: extractelement
  %r = xor <32 x i4> %a, <i4 -1, i4 -1, i4 -1, i4 -1, i4 -1, i4 -1, i4 -1, i4 -1, i4 -1, i4 -1, i4 -1, i4 -1, i4 -1, i4 -1, i4 -1, i4 -1, i4 -1, i4 -1, i4 -1, i4 -1, i4 -1, i4 -1, i4 -1, i4 -1, i4 -1, i4 -1, i4 -1, i4 -1, i4 -1, i4 -1, i4 -1, i4 -1>
  ret <32 x i4> %r
}
