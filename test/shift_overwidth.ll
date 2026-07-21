; Differential test: the at/over-width shift-amount edge case.
;
; Per the U2-B decision, an out-of-range shift amount (count >= field width N,
; which is poison in the LangRef) is defined by *masking the amount to N* --
; i.e. the effective count is `amount & (N-1)`. These kernels encode that
; masking in-IR, so the scalar reference is defined even when the raw input
; amount is >= N. Because they self-mask, the harness recognizes the `_ovf`
; marker and feeds them the FULL unclamped amount range (the structured all-
; ones / high-bit fills and the random pairs all contain counts >= N), so the
; over-width path is actually exercised rather than clamped away -- the
; candidate SWAR lowering must agree with the masked scalar reference for
; every such input.
;
; Companion to diff/{shl,lshr,ashr}.ll, which cover the in-range case (amounts
; the harness clamps into [0, N-1]). i1 is omitted: its only in-range amount is
; 0, so shift is identity with no over-width distinction to make.
;
; Lives at the test root (not diff/) because coverage_check.py treats every
; diff/*.ll filename as an operation name requiring per-width shape tests.
; RUN: %python "%diff_runner" --opt "%opt" --lli "%lli" --plugin "%nybbler" "%s" | %FileCheck "%s"
; CHECK: ALL PASS

; --- shl: byte-multiple and padded ---
define <8 x i2> @shl_i2_ovf(<8 x i2> %a, <8 x i2> %b) {
  %amt = and <8 x i2> %b, splat (i2 1)
  %r = shl <8 x i2> %a, %amt
  ret <8 x i2> %r
}

define <32 x i4> @shl_i4_ovf(<32 x i4> %a, <32 x i4> %b) {
  %amt = and <32 x i4> %b, splat (i4 3)
  %r = shl <32 x i4> %a, %amt
  ret <32 x i4> %r
}

define <5 x i2> @shl_i2_ovf_pad(<5 x i2> %a, <5 x i2> %b) {
  %amt = and <5 x i2> %b, splat (i2 1)
  %r = shl <5 x i2> %a, %amt
  ret <5 x i2> %r
}

define <3 x i4> @shl_i4_ovf_pad(<3 x i4> %a, <3 x i4> %b) {
  %amt = and <3 x i4> %b, splat (i4 3)
  %r = shl <3 x i4> %a, %amt
  ret <3 x i4> %r
}

; --- lshr: byte-multiple and padded ---
define <8 x i2> @lshr_i2_ovf(<8 x i2> %a, <8 x i2> %b) {
  %amt = and <8 x i2> %b, splat (i2 1)
  %r = lshr <8 x i2> %a, %amt
  ret <8 x i2> %r
}

define <32 x i4> @lshr_i4_ovf(<32 x i4> %a, <32 x i4> %b) {
  %amt = and <32 x i4> %b, splat (i4 3)
  %r = lshr <32 x i4> %a, %amt
  ret <32 x i4> %r
}

define <7 x i2> @lshr_i2_ovf_pad(<7 x i2> %a, <7 x i2> %b) {
  %amt = and <7 x i2> %b, splat (i2 1)
  %r = lshr <7 x i2> %a, %amt
  ret <7 x i2> %r
}

define <5 x i4> @lshr_i4_ovf_pad(<5 x i4> %a, <5 x i4> %b) {
  %amt = and <5 x i4> %b, splat (i4 3)
  %r = lshr <5 x i4> %a, %amt
  ret <5 x i4> %r
}

; --- ashr: byte-multiple and padded (sign-fill on the vacated high bits) ---
define <8 x i2> @ashr_i2_ovf(<8 x i2> %a, <8 x i2> %b) {
  %amt = and <8 x i2> %b, splat (i2 1)
  %r = ashr <8 x i2> %a, %amt
  ret <8 x i2> %r
}

define <32 x i4> @ashr_i4_ovf(<32 x i4> %a, <32 x i4> %b) {
  %amt = and <32 x i4> %b, splat (i4 3)
  %r = ashr <32 x i4> %a, %amt
  ret <32 x i4> %r
}

define <7 x i2> @ashr_i2_ovf_pad(<7 x i2> %a, <7 x i2> %b) {
  %amt = and <7 x i2> %b, splat (i2 1)
  %r = ashr <7 x i2> %a, %amt
  ret <7 x i2> %r
}

define <3 x i4> @ashr_i4_ovf_pad(<3 x i4> %a, <3 x i4> %b) {
  %amt = and <3 x i4> %b, splat (i4 3)
  %r = ashr <3 x i4> %a, %amt
  ret <3 x i4> %r
}
