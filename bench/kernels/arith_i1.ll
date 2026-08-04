; arith_i1 -- 1-bit field arithmetic pass over a byte stream.
;
; NYB_DESC: <128 x i1> add / sub chain
; NYB_OUT_BYTES_PER_VEC: 16
;
; DEGENERATE RESULT -- read the number with the caveat, same as mask_i1 and
; shift_i1. A 1-bit field has no room for a carry or borrow to go anywhere:
; add and sub on i1 are both just xor. LLVM already knows this and emits the
; same code with or without nybbler, so expect ~1.0x here. This is the
; correct result, not a broken measurement -- the arithmetic speedup claim
; rests on field_i2 and arith_i4, where a carry/borrow actually has somewhere
; to go and the legalizer's per-lane extract/insert cost shows up. See
; docs/benchmarks.md and README.md's note that "almost everything at i1"
; degenerates this way.

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

  %sum = add <128 x i1> %va, %vb
  %res = sub <128 x i1> %sum, %va

  %lo = bitcast <128 x i1> %res to <16 x i8>
  store <16 x i8> %lo, ptr %po, align 16

  %i.next = add nuw i64 %i, 1
  %more = icmp ult i64 %i.next, %nvec
  br i1 %more, label %loop, label %done

done:
  ret void
}
