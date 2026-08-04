; bitwise_i2 -- 2-bit field bitwise pass over a byte stream.
;
; NYB_DESC: <64 x i2> xor / and / or mask pass
; NYB_OUT_BYTES_PER_VEC: 16
;
; Same shape as mask_i1, one width up. Bitwise ops are bit-independent, so
; LLVM's own legalizer already reinterprets <K x i2> bitwise as bytes without
; scalarizing -- expect ~1.0x, same story as mask_i1 and bitwise_i4. The
; speedup claim rests on the masked paths (arith_i1/field_i2/arith_i4,
; shift_i1/shift_i2/shift_i4), not here. See docs/benchmarks.md.

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

  %x = xor <64 x i2> %va, %vb
  %y = and <64 x i2> %x, %va
  %res = or <64 x i2> %y, %vb

  %lo = bitcast <64 x i2> %res to <16 x i8>
  store <16 x i8> %lo, ptr %po, align 16

  %i.next = add nuw i64 %i, 1
  %more = icmp ult i64 %i.next, %nvec
  br i1 %more, label %loop, label %done

done:
  ret void
}
