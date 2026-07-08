; SWAR add for i4, i2, and i1 narrow fields.
; RUN: %opt -load-pass-plugin "%nybbler" -passes=nybbler "%s" -S | %FileCheck "%s"

; ---------------------------------------------------------------------------
; i4 add
; ---------------------------------------------------------------------------
; Expected carrier sequence (one byte holds two i4 fields):
;   lmask = splat 0x77, hmask = splat 0x88
;   alo   = a & lmask
;   blo   = b & lmask
;   sum   = alo + blo          (no inter-field carry; top bit was clear)
;   top   = (a ^ b) & hmask    (top bit of each field = XOR of input top bits)
;   result = sum ^ top

define <32 x i4> @add_i4(<32 x i4> %a, <32 x i4> %b) {
; CHECK-LABEL: @add_i4
; CHECK: bitcast <32 x i4> %a to <16 x i8>
; CHECK: bitcast <32 x i4> %b to <16 x i8>
; CHECK: and <16 x i8> {{.*}}, splat (i8 119)
; CHECK: and <16 x i8> {{.*}}, splat (i8 119)
; CHECK: add <16 x i8>
; CHECK: xor <16 x i8>
; CHECK: and <16 x i8> {{.*}}, splat (i8 -120)
; CHECK: xor <16 x i8>
; CHECK: bitcast <16 x i8> %{{.*}} to <32 x i4>
; CHECK-NOT: extractelement
  %r = add <32 x i4> %a, %b
  ret <32 x i4> %r
}

; ---------------------------------------------------------------------------
; i2 add
; ---------------------------------------------------------------------------
; lmask = splat 0x55 (85), hmask = splat 0xAA (-86 / 170)

define <8 x i2> @add_i2(<8 x i2> %a, <8 x i2> %b) {
; CHECK-LABEL: @add_i2
; CHECK: bitcast <8 x i2> %a to <2 x i8>
; CHECK: bitcast <8 x i2> %b to <2 x i8>
; CHECK: and <2 x i8> {{.*}}, splat (i8 85)
; CHECK: and <2 x i8> {{.*}}, splat (i8 85)
; CHECK: add <2 x i8>
; CHECK: xor <2 x i8>
; CHECK: and <2 x i8> {{.*}}, splat (i8 -86)
; CHECK: xor <2 x i8>
; CHECK: bitcast <2 x i8> %{{.*}} to <8 x i2>
; CHECK-NOT: extractelement
  %r = add <8 x i2> %a, %b
  ret <8 x i2> %r
}

; ---------------------------------------------------------------------------
; i1 add  ->  xor  (addition mod 2 == XOR; no masking needed)
; ---------------------------------------------------------------------------

define <64 x i1> @add_i1(<64 x i1> %a, <64 x i1> %b) {
; CHECK-LABEL: @add_i1
; CHECK: bitcast <64 x i1> %a to <8 x i8>
; CHECK: bitcast <64 x i1> %b to <8 x i8>
; CHECK: xor <8 x i8>
; CHECK: bitcast <8 x i8> %{{.*}} to <64 x i1>
; CHECK-NOT: add
; CHECK-NOT: extractelement
  %r = add <64 x i1> %a, %b
  ret <64 x i1> %r
}
