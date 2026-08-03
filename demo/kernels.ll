; End-to-end demo kernels: packed 4-bit fields, processed 32 at a time.
;
; Both kernels are ordinary LLVM IR that any frontend could emit -- the only
; unusual thing about them is the `<32 x i4>` type, which no real target can
; execute. Without nybbler the legalizer scalarizes them into per-nibble
; extract/insert chains; with nybbler they become byte-carrier SWAR sequences
; the backend lowers straight to SIMD.
;
; The loop is deliberate: it makes the mask-folding claim observable. Every
; field mask is loop-invariant, so a correct lowering hoists them out and the
; loop body contains only data instructions.
;
; Buffers are addressed as 16-byte chunks (16 bytes = 32 nibbles = one carrier).
; Loads are `align 1` so the demo runs on plain malloc'd memory.

; out[i] = a[i] + b[i], per 4-bit field, with carries confined to their field.
define void @nibble_add(ptr noalias %a, ptr noalias %b, ptr noalias %out,
                        i64 %nchunks) {
entry:
  %empty = icmp eq i64 %nchunks, 0
  br i1 %empty, label %exit, label %loop

loop:
  %i = phi i64 [ 0, %entry ], [ %inext, %loop ]
  %pa = getelementptr <16 x i8>, ptr %a, i64 %i
  %pb = getelementptr <16 x i8>, ptr %b, i64 %i
  %po = getelementptr <16 x i8>, ptr %out, i64 %i
  %va = load <16 x i8>, ptr %pa, align 1
  %vb = load <16 x i8>, ptr %pb, align 1
  %na = bitcast <16 x i8> %va to <32 x i4>
  %nb = bitcast <16 x i8> %vb to <32 x i4>
  %sum = add <32 x i4> %na, %nb
  %vs = bitcast <32 x i4> %sum to <16 x i8>
  store <16 x i8> %vs, ptr %po, align 1
  %inext = add i64 %i, 1
  %last = icmp eq i64 %inext, %nchunks
  br i1 %last, label %exit, label %loop

exit:
  ret void
}

; out[i] = 0xF in each field where a < b (unsigned), 0 otherwise.
;
; The field-mask idiom: the compare's <32 x i1> result is immediately sext'd
; back to the operand width. nybbler hands the mask its compare handler already
; computed straight to the sext, so the unrepresentable <32 x i1> is never
; materialized -- see the fold in lowerNarrowOp.
define void @nibble_lt_mask(ptr noalias %a, ptr noalias %b, ptr noalias %out,
                            i64 %nchunks) {
entry:
  %empty = icmp eq i64 %nchunks, 0
  br i1 %empty, label %exit, label %loop

loop:
  %i = phi i64 [ 0, %entry ], [ %inext, %loop ]
  %pa = getelementptr <16 x i8>, ptr %a, i64 %i
  %pb = getelementptr <16 x i8>, ptr %b, i64 %i
  %po = getelementptr <16 x i8>, ptr %out, i64 %i
  %va = load <16 x i8>, ptr %pa, align 1
  %vb = load <16 x i8>, ptr %pb, align 1
  %na = bitcast <16 x i8> %va to <32 x i4>
  %nb = bitcast <16 x i8> %vb to <32 x i4>
  %lt = icmp ult <32 x i4> %na, %nb
  %m = sext <32 x i1> %lt to <32 x i4>
  %vm = bitcast <32 x i4> %m to <16 x i8>
  store <16 x i8> %vm, ptr %po, align 1
  %inext = add i64 %i, 1
  %last = icmp eq i64 %inext, %nchunks
  br i1 %last, label %exit, label %loop

exit:
  ret void
}
