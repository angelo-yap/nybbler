; mask_i1 -- 1-bit mask/bitwise pass over a stream.
;
; NYB_DESC: <128 x i1> xor / and / or mask pass
; NYB_OUT_BYTES_PER_VEC: 16
;
; The bit-stream case: a chain of pure bitwise ops over 128 single-bit fields,
; the shape a Parabix-style mask pass has.
;
; Expect this one to come out at roughly 1.0x, and that is the correct
; result rather than a broken measurement. Bitwise ops are bit-independent, so
; LLVM's own legalizer already reinterprets <K x i1> bitwise as bytes without
; scalarizing -- nybbler emits the same thing it would have. What nybbler adds
; here is that the reinterpretation is explicit and guaranteed rather than
; dependent on the legalizer's discretion. The speedup claim rests on the
; masked paths (add/sub/shift/ashr), which is exactly what arith_i4 and
; field_i2 measure. See docs/benchmarks.md.

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

  %x = xor <128 x i1> %va, %vb
  %y = and <128 x i1> %x, %va
  %res = or <128 x i1> %y, %vb

  %lo = bitcast <128 x i1> %res to <16 x i8>
  store <16 x i8> %lo, ptr %po, align 16

  %i.next = add nuw i64 %i, 1
  %more = icmp ult i64 %i.next, %nvec
  br i1 %more, label %loop, label %done

done:
  ret void
}
