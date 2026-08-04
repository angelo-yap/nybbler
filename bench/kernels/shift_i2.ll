; shift_i2 -- 2-bit field shift pass over a byte stream.
;
; NYB_DESC: <64 x i2> shl / lshr chain, per-field amount masked to [0, 1]
; NYB_OUT_BYTES_PER_VEC: 16
;
; Isolates the shift class from field_i2's combined add/lshr/sub chain: pure
; shl then lshr, no arithmetic mixed in. The amount is masked to [0, 1] in
; IR (the same idiom as field_i2.ll and arith_i4.ll) so every field's shift
; stays in range -- an amount >= N is poison, which has no defined value for
; the scalarized baseline to produce, so the two builds' checksums could
; legitimately disagree for a non-reason if left unmasked.

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

  %amt = and <64 x i2> %vb, splat (i2 1)
  %shl_res = shl <64 x i2> %va, %amt
  %res = lshr <64 x i2> %shl_res, %amt

  %lo = bitcast <64 x i2> %res to <16 x i8>
  store <16 x i8> %lo, ptr %po, align 16

  %i.next = add nuw i64 %i, 1
  %more = icmp ult i64 %i.next, %nvec
  br i1 %more, label %loop, label %done

done:
  ret void
}
