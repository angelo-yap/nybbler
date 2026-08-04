; shift_i1 -- 1-bit field shift pass over a byte stream.
;
; NYB_DESC: <128 x i1> shl / lshr chain (necessarily by 0)
; NYB_OUT_BYTES_PER_VEC: 16
;
; NECESSARILY DEGENERATE -- a 1-bit field has exactly one in-range shift
; amount: 0. shl/lshr by 0 is the identity, so there is nothing for nybbler
; to do differently from the default legalizer at this width; expect ~1.0x.
; This is the honest result of there being no nonzero in-range amount to
; mask into [0, N-1] when N=1, not a broken kernel. The shift speedup claim
; rests on shift_i2 and shift_i4, where the field actually has room to shift
; within itself.

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
  %va = bitcast <16 x i8> %la to <128 x i1>
  %vb = bitcast <16 x i8> %lb to <128 x i1>

  %amt = and <128 x i1> %vb, splat (i1 0)
  %shl_res = shl <128 x i1> %va, %amt
  %res = lshr <128 x i1> %shl_res, %amt

  %lo = bitcast <128 x i1> %res to <16 x i8>
  store <16 x i8> %lo, ptr %po, align 16

  %i.next = add nuw i64 %i, 1
  %more = icmp ult i64 %i.next, %nvec
  br i1 %more, label %loop, label %done

done:
  ret void
}
