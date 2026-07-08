; Shape test: i1 sub remaps to xor before dispatch -> bitwise carrier path.
; RUN: %opt -load-pass-plugin "%nybbler" -passes=nybbler "%s" -S | %FileCheck "%s"

define <64 x i1> @sub_i1(<64 x i1> %a, <64 x i1> %b) {
; CHECK-LABEL: @sub_i1
; CHECK: bitcast <64 x i1> %a to <8 x i8>
; CHECK: bitcast <64 x i1> %b to <8 x i8>
; CHECK: xor <8 x i8>
; CHECK: bitcast <8 x i8> %{{.*}} to <64 x i1>
; CHECK-NOT: sub
; CHECK-NOT: extractelement
  %r = sub <64 x i1> %a, %b
  ret <64 x i1> %r
}
