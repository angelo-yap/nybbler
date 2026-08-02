; field_i2 -- 2-bit field operation over a byte stream.
;
; NYB_DESC: <64 x i2> add / lshr-by-field / sub chain
; NYB_OUT_BYTES_PER_VEC: 16
;
; Same op chain as arith_i4 but at N=2, so the mask constants change (0x55 /
; 0xAA instead of 0x77 / 0x88) and the barrel-shift loop runs a different
; number of steps. Four fields per byte means the scalarized baseline has
; twice as many lanes to extract and re-insert, which is where the widening
; gap between the two builds at narrower N comes from.
;
; lshr rather than shl so the logical-shift path is covered in the other
; direction from arith_i4; the amount is masked to [0, 1] to stay in range
; (see arith_i4.ll for why poison amounts are excluded).

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
  %va = bitcast <16 x i8> %la to <64 x i2>
  %vb = bitcast <16 x i8> %lb to <64 x i2>

  %sum = add <64 x i2> %va, %vb
  %amt = and <64 x i2> %vb, splat (i2 1)
  %shifted = lshr <64 x i2> %sum, %amt
  %res = sub <64 x i2> %shifted, %va

  %lo = bitcast <64 x i2> %res to <16 x i8>
  store <16 x i8> %lo, ptr %po, align 16

  %i.next = add nuw i64 %i, 1
  %more = icmp ult i64 %i.next, %nvec
  br i1 %more, label %loop, label %done

done:
  ret void
}
