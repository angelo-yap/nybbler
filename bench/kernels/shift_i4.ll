; shift_i4 -- 4-bit field shift pass over a byte stream.
;
; NYB_DESC: <32 x i4> shl / lshr chain, per-field amount masked to [0, 3]
; NYB_OUT_BYTES_PER_VEC: 16
;
; Isolates the shift class from arith_i4's combined add/shl/sub chain: pure
; shl then lshr, no arithmetic mixed in. Amount masked to [0, 3] for the same
; poison-avoidance reason as shift_i2.ll and arith_i4.ll -- see those files.

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

  %amt = and <32 x i4> %vb, splat (i4 3)
  %shl_res = shl <32 x i4> %va, %amt
  %res = lshr <32 x i4> %shl_res, %amt

  %lo = bitcast <32 x i4> %res to <16 x i8>
  store <16 x i8> %lo, ptr %po, align 16

  %i.next = add nuw i64 %i, 1
  %more = icmp ult i64 %i.next, %nvec
  br i1 %more, label %loop, label %done

done:
  ret void
}
