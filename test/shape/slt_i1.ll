; Shape test: narrow i1 icmp slt lowers via single-bit logic (no carrier
; bitcast): -1 <s 0 is the only true case, so slt(a,b) = a & ~b.
; RUN: %opt -load-pass-plugin "%nybbler" -passes=nybbler "%s" -S | %FileCheck "%s"

define <64 x i1> @slt_i1(<64 x i1> %a, <64 x i1> %b) {
; CHECK-LABEL: @slt_i1
; CHECK: %slt.i1.notb = xor <64 x i1> %b, splat (i1 true)
; CHECK: %slt.i1.result = and <64 x i1> %a, %slt.i1.notb
; CHECK-NOT: extractelement
  %r = icmp slt <64 x i1> %a, %b
  ret <64 x i1> %r
}
