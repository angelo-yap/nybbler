; Shape test: narrow i2 ashr lowers onto a <2 x i8> carrier
; (<8 x i2> = 16 bits) with no illegal-type fallback.
; RUN: %opt -load-pass-plugin "%nybbler" -passes=nybbler "%s" -S | %FileCheck "%s"
; XFAIL: *
; Pending Slice 2 (arithmetic/shift/compare lowering, see README) -- pass
; does not lower this op yet, so it falls through unchanged.

define <8 x i2> @ashr_i2(<8 x i2> %a, <8 x i2> %b) {
; CHECK-LABEL: @ashr_i2
; CHECK: bitcast <8 x i2> %a to <2 x i8>
; CHECK: bitcast <8 x i2> %b to <2 x i8>
; CHECK: ashr <2 x i8>
; CHECK: and <2 x i8>
; CHECK: bitcast <2 x i8> %{{.*}} to <8 x i2>
; CHECK-NOT: extractelement
  %r = ashr <8 x i2> %a, %b
  ret <8 x i2> %r
}
