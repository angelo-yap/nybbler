; Explicit boundary-value assertions for the carrier lowering.
;
; The differential harness sweeps the all-zero / all-ones / sign-boundary
; STRUCTURED fills across every op, but does so against an interpreted
; reference. This test pins the *exact* expected result bytes for the
; semantically interesting edge cases -- all-zero and all-ones fields, and the
; signed high-bit boundary for slt/ashr sign handling -- as a human-readable
; golden test, independent of any reference interpreter: the module is lowered
; by the pass and the executed output is checked directly against literals.
;
; Each kernel packs its fields into exactly one carrier byte, printed as two
; hex digits; the CHECK block below lists the expected bytes in call order.
; RUN: %opt -load-pass-plugin "%nybbler" -passes=nybbler "%s" -S -o "%t.ll"
; RUN: %lli "%t.ll" | %FileCheck "%s"

; i4 fields -------------------------------------------------------------------
; eq(0,0)        -> true  -> 0xF per field
; CHECK: ff
; eq(0xF,0x0)    -> false -> 0x0
; CHECK: 00
; and(0xF,0x0)   -> 0x0                     (all-ones & all-zero)
; CHECK: 00
; or(0xF,0x0)    -> 0xF                     (all-ones | all-zero)
; CHECK: ff
; add(15,15)     -> 30 mod 16 = 14 = 0xE    (all-ones wrap)
; CHECK: ee
; sub(0,1)       -> -1 = 15 = 0xF           (borrow out of zero)
; CHECK: ff
; slt(-8,+7)     -> true  -> 0xF            (min < max, signed)
; CHECK: ff
; slt(+7,-8)     -> false -> 0x0            (max < min, signed)
; CHECK: 00
; ashr(-8,3)     -> -1 = 0xF                (sign fill to full width)
; CHECK: ff
; ashr(+7,1)     -> 3 = 0x3                 (positive, no sign fill)
; CHECK: 33
; shl(1,3)       -> 8 = 0x8                 (bit walks to field top)
; CHECK: 88

; i2 fields -------------------------------------------------------------------
; add(3,3)       -> 6 mod 4 = 2 = 0b10 x4 -> 0xAA
; CHECK: aa
; slt(-2,+1)     -> true  -> 0b11 x4      -> 0xFF
; CHECK: ff
; ashr(-2,1)     -> -1 = 0b11 x4          -> 0xFF
; CHECK: ff

; i1 fields -------------------------------------------------------------------
; add(1,1)       -> 0 (add mod 2 == xor)  -> 0x00
; CHECK: 00
; eq(1,1)        -> true                  -> 0xFF
; CHECK: ff

define <2 x i4> @eq_zero_i4()    { %r = icmp eq  <2 x i4> zeroinitializer, zeroinitializer
                                    %s = sext <2 x i1> %r to <2 x i4> ret <2 x i4> %s }
define <2 x i4> @eq_ones0_i4()   { %r = icmp eq  <2 x i4> <i4 -1, i4 -1>, zeroinitializer
                                    %s = sext <2 x i1> %r to <2 x i4> ret <2 x i4> %s }
define <2 x i4> @and_ones0_i4()  { %r = and <2 x i4> <i4 -1, i4 -1>, zeroinitializer ret <2 x i4> %r }
define <2 x i4> @or_ones0_i4()   { %r = or  <2 x i4> <i4 -1, i4 -1>, zeroinitializer ret <2 x i4> %r }
define <2 x i4> @add_ones_i4()   { %r = add <2 x i4> <i4 -1, i4 -1>, <i4 -1, i4 -1> ret <2 x i4> %r }
define <2 x i4> @sub_borrow_i4() { %r = sub <2 x i4> zeroinitializer, <i4 1, i4 1> ret <2 x i4> %r }
define <2 x i4> @slt_bound_i4()  { %r = icmp slt <2 x i4> <i4 -8, i4 -8>, <i4 7, i4 7>
                                    %s = sext <2 x i1> %r to <2 x i4> ret <2 x i4> %s }
define <2 x i4> @slt_rev_i4()    { %r = icmp slt <2 x i4> <i4 7, i4 7>, <i4 -8, i4 -8>
                                    %s = sext <2 x i1> %r to <2 x i4> ret <2 x i4> %s }
define <2 x i4> @ashr_fill_i4()  { %r = ashr <2 x i4> <i4 -8, i4 -8>, <i4 3, i4 3> ret <2 x i4> %r }
define <2 x i4> @ashr_pos_i4()   { %r = ashr <2 x i4> <i4 7, i4 7>, <i4 1, i4 1> ret <2 x i4> %r }
define <2 x i4> @shl_top_i4()    { %r = shl  <2 x i4> <i4 1, i4 1>, <i4 3, i4 3> ret <2 x i4> %r }

define <4 x i2> @add_ones_i2()   { %r = add <4 x i2> <i2 -1, i2 -1, i2 -1, i2 -1>, <i2 -1, i2 -1, i2 -1, i2 -1> ret <4 x i2> %r }
define <4 x i2> @slt_bound_i2()  { %r = icmp slt <4 x i2> <i2 -2, i2 -2, i2 -2, i2 -2>, <i2 1, i2 1, i2 1, i2 1>
                                    %s = sext <4 x i1> %r to <4 x i2> ret <4 x i2> %s }
define <4 x i2> @ashr_fill_i2()  { %r = ashr <4 x i2> <i2 -2, i2 -2, i2 -2, i2 -2>, <i2 1, i2 1, i2 1, i2 1> ret <4 x i2> %r }

define <8 x i1> @add_ones_i1()   { %r = add <8 x i1> <i1 1, i1 1, i1 1, i1 1, i1 1, i1 1, i1 1, i1 1>, <i1 1, i1 1, i1 1, i1 1, i1 1, i1 1, i1 1, i1 1> ret <8 x i1> %r }
define <8 x i1> @eq_ones_i1()    { %r = icmp eq <8 x i1> <i1 1, i1 1, i1 1, i1 1, i1 1, i1 1, i1 1, i1 1>, <i1 1, i1 1, i1 1, i1 1, i1 1, i1 1, i1 1, i1 1> ret <8 x i1> %r }

@.hex = private constant [6 x i8] c"%02x\0A\00"
declare i32 @printf(i8*, ...)
define i32 @main() {
  %v0 = call <2 x i4> @eq_zero_i4()
  %b0 = bitcast <2 x i4> %v0 to i8
  %z0 = zext i8 %b0 to i32
  call i32 (i8*, ...) @printf(i8* getelementptr([6 x i8], [6 x i8]* @.hex, i32 0, i32 0), i32 %z0)
  %v1 = call <2 x i4> @eq_ones0_i4()
  %b1 = bitcast <2 x i4> %v1 to i8
  %z1 = zext i8 %b1 to i32
  call i32 (i8*, ...) @printf(i8* getelementptr([6 x i8], [6 x i8]* @.hex, i32 0, i32 0), i32 %z1)
  %v2 = call <2 x i4> @and_ones0_i4()
  %b2 = bitcast <2 x i4> %v2 to i8
  %z2 = zext i8 %b2 to i32
  call i32 (i8*, ...) @printf(i8* getelementptr([6 x i8], [6 x i8]* @.hex, i32 0, i32 0), i32 %z2)
  %v3 = call <2 x i4> @or_ones0_i4()
  %b3 = bitcast <2 x i4> %v3 to i8
  %z3 = zext i8 %b3 to i32
  call i32 (i8*, ...) @printf(i8* getelementptr([6 x i8], [6 x i8]* @.hex, i32 0, i32 0), i32 %z3)
  %v4 = call <2 x i4> @add_ones_i4()
  %b4 = bitcast <2 x i4> %v4 to i8
  %z4 = zext i8 %b4 to i32
  call i32 (i8*, ...) @printf(i8* getelementptr([6 x i8], [6 x i8]* @.hex, i32 0, i32 0), i32 %z4)
  %v5 = call <2 x i4> @sub_borrow_i4()
  %b5 = bitcast <2 x i4> %v5 to i8
  %z5 = zext i8 %b5 to i32
  call i32 (i8*, ...) @printf(i8* getelementptr([6 x i8], [6 x i8]* @.hex, i32 0, i32 0), i32 %z5)
  %v6 = call <2 x i4> @slt_bound_i4()
  %b6 = bitcast <2 x i4> %v6 to i8
  %z6 = zext i8 %b6 to i32
  call i32 (i8*, ...) @printf(i8* getelementptr([6 x i8], [6 x i8]* @.hex, i32 0, i32 0), i32 %z6)
  %v7 = call <2 x i4> @slt_rev_i4()
  %b7 = bitcast <2 x i4> %v7 to i8
  %z7 = zext i8 %b7 to i32
  call i32 (i8*, ...) @printf(i8* getelementptr([6 x i8], [6 x i8]* @.hex, i32 0, i32 0), i32 %z7)
  %v8 = call <2 x i4> @ashr_fill_i4()
  %b8 = bitcast <2 x i4> %v8 to i8
  %z8 = zext i8 %b8 to i32
  call i32 (i8*, ...) @printf(i8* getelementptr([6 x i8], [6 x i8]* @.hex, i32 0, i32 0), i32 %z8)
  %v9 = call <2 x i4> @ashr_pos_i4()
  %b9 = bitcast <2 x i4> %v9 to i8
  %z9 = zext i8 %b9 to i32
  call i32 (i8*, ...) @printf(i8* getelementptr([6 x i8], [6 x i8]* @.hex, i32 0, i32 0), i32 %z9)
  %v10 = call <2 x i4> @shl_top_i4()
  %b10 = bitcast <2 x i4> %v10 to i8
  %z10 = zext i8 %b10 to i32
  call i32 (i8*, ...) @printf(i8* getelementptr([6 x i8], [6 x i8]* @.hex, i32 0, i32 0), i32 %z10)
  %v11 = call <4 x i2> @add_ones_i2()
  %b11 = bitcast <4 x i2> %v11 to i8
  %z11 = zext i8 %b11 to i32
  call i32 (i8*, ...) @printf(i8* getelementptr([6 x i8], [6 x i8]* @.hex, i32 0, i32 0), i32 %z11)
  %v12 = call <4 x i2> @slt_bound_i2()
  %b12 = bitcast <4 x i2> %v12 to i8
  %z12 = zext i8 %b12 to i32
  call i32 (i8*, ...) @printf(i8* getelementptr([6 x i8], [6 x i8]* @.hex, i32 0, i32 0), i32 %z12)
  %v13 = call <4 x i2> @ashr_fill_i2()
  %b13 = bitcast <4 x i2> %v13 to i8
  %z13 = zext i8 %b13 to i32
  call i32 (i8*, ...) @printf(i8* getelementptr([6 x i8], [6 x i8]* @.hex, i32 0, i32 0), i32 %z13)
  %v14 = call <8 x i1> @add_ones_i1()
  %b14 = bitcast <8 x i1> %v14 to i8
  %z14 = zext i8 %b14 to i32
  call i32 (i8*, ...) @printf(i8* getelementptr([6 x i8], [6 x i8]* @.hex, i32 0, i32 0), i32 %z14)
  %v15 = call <8 x i1> @eq_ones_i1()
  %b15 = bitcast <8 x i1> %v15 to i8
  %z15 = zext i8 %b15 to i32
  call i32 (i8*, ...) @printf(i8* getelementptr([6 x i8], [6 x i8]* @.hex, i32 0, i32 0), i32 %z15)
  ret i32 0
}
