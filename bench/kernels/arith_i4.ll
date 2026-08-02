; arith_i4 -- nibble-packed i4 arithmetic over a byte stream.
;
; NYB_DESC: <32 x i4> add / shl-by-field / sub chain
; NYB_OUT_BYTES_PER_VEC: 16
;
; Exercises the masked SWAR paths that nybbler exists for: lowerAdd's carry
; containment, lowerShift's per-field boundary masking, and lowerSub's borrow
; absorber -- three different mask formulas over the same carrier.
;
; Kernel shape (see docs/benchmarks.md "Why the loads are <16 x i8>"): memory
; traffic stays at byte type and the narrow type appears only between the
; bitcast pair around the op chain. A <32 x i4> load/store would be legalized
; by scalarizing in *both* builds and would swamp the measurement.
;
; The shift amount is masked to [0, 3] in IR so every field's shift is in
; range. An amount >= N is poison, which has no defined value for the
; scalarized baseline to produce -- the two builds' checksums could then
; legitimately disagree and the run would fail for a non-reason.

define void @nyb_kernel(ptr noalias %a, ptr noalias %b, ptr noalias %o, i64 %nvec) {
entry:
  %nonempty = icmp ugt i64 %nvec, 0
  br i1 %nonempty, label %loop, label %done

loop:
  %i = phi i64 [ 0, %entry ], [ %i.next, %loop ]
  %pa = getelementptr inbounds <16 x i8>, ptr %a, i64 %i
  %pb = getelementptr inbounds <16 x i8>, ptr %b, i64 %i
  %po = getelementptr inbounds <16 x i8>, ptr %o, i64 %i

  %la = load <16 x i8>, ptr %pa, align 16
  %lb = load <16 x i8>, ptr %pb, align 16
  %va = bitcast <16 x i8> %la to <32 x i4>
  %vb = bitcast <16 x i8> %lb to <32 x i4>

  %sum = add <32 x i4> %va, %vb
  %amt = and <32 x i4> %vb, splat (i4 3)
  %shifted = shl <32 x i4> %sum, %amt
  %res = sub <32 x i4> %shifted, %va

  %lo = bitcast <32 x i4> %res to <16 x i8>
  store <16 x i8> %lo, ptr %po, align 16

  %i.next = add nuw i64 %i, 1
  %more = icmp ult i64 %i.next, %nvec
  br i1 %more, label %loop, label %done

done:
  ret void
}
