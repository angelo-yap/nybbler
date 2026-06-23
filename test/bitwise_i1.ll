; Narrow i1 bitwise ops lower to an <8 x i8> carrier (<64 x i1> = 64 bits).
; RUN: %opt -load-pass-plugin "%nybbler" -passes=nybbler "%s" -S | %FileCheck "%s"

define <64 x i1> @and_i1(<64 x i1> %a, <64 x i1> %b) {
; CHECK-LABEL: @and_i1
; CHECK: bitcast <64 x i1> %a to <8 x i8>
; CHECK: bitcast <64 x i1> %b to <8 x i8>
; CHECK: and <8 x i8>
; CHECK: bitcast <8 x i8> %{{.*}} to <64 x i1>
; CHECK-NOT: extractelement
  %r = and <64 x i1> %a, %b
  ret <64 x i1> %r
}

define <64 x i1> @or_i1(<64 x i1> %a, <64 x i1> %b) {
; CHECK-LABEL: @or_i1
; CHECK: bitcast <64 x i1> %a to <8 x i8>
; CHECK: bitcast <64 x i1> %b to <8 x i8>
; CHECK: or <8 x i8>
; CHECK: bitcast <8 x i8> %{{.*}} to <64 x i1>
; CHECK-NOT: extractelement
  %r = or <64 x i1> %a, %b
  ret <64 x i1> %r
}

define <64 x i1> @xor_i1(<64 x i1> %a, <64 x i1> %b) {
; CHECK-LABEL: @xor_i1
; CHECK: bitcast <64 x i1> %a to <8 x i8>
; CHECK: bitcast <64 x i1> %b to <8 x i8>
; CHECK: xor <8 x i8>
; CHECK: bitcast <8 x i8> %{{.*}} to <64 x i1>
; CHECK-NOT: extractelement
  %r = xor <64 x i1> %a, %b
  ret <64 x i1> %r
}
