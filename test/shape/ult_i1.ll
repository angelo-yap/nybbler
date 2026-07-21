; Shape test: narrow i1 icmp ult lowers via single-bit logic (no carrier
; bitcast): 0 <u 1 is the only true case, so ult(a,b) = ~a & b.
; RUN: %opt -load-pass-plugin "%nybbler" -passes=nybbler "%s" -S | %FileCheck "%s"

define <64 x i1> @ult_i1(<64 x i1> %a, <64 x i1> %b) {
; CHECK-LABEL: @ult_i1
; CHECK: %ult.i1.nota = xor <64 x i1> %a, splat (i1 true)
; CHECK: %ult.i1.result = and <64 x i1> %ult.i1.nota, %b
; CHECK-NOT: extractelement
  %r = icmp ult <64 x i1> %a, %b
  ret <64 x i1> %r
}
