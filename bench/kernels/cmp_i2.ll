; cmp_i2 -- 2-bit field unsigned compare to a packed bitmask.
;
; NYB_DESC: <64 x i2> icmp ult -> packed <8 x i8> bitmask  (PARTIAL, see below)
; NYB_OUT_BYTES_PER_VEC: 8
;
; Same structure as cmp_i4, one width down. PARTIAL RESULT -- read with the
; caveat. nybbler lowers the icmp itself, but not the
; `bitcast <64 x i1> to <8 x i8>` that materializes the result into a packed
; mask; that materialization is expensive in both builds and dominates the
; loop, understating the compare lowering considerably. See the Limitations
; section of docs/benchmarks.md and cmp_i4.ll's header for the fuller
; explanation, which applies here unchanged.

define void @nyb_kernel(ptr noalias %a, ptr noalias %b, ptr noalias %o, i64 %nvec) {
entry:
  %nonempty = icmp ugt i64 %nvec, 0
  br i1 %nonempty, label %loop, label %done

loop:
  %i = phi i64 [ 0, %entry ], [ %i.next, %loop ]
  %pa = getelementptr inbounds <16 x i8>, ptr %a, i64 %i
  %pb = getelementptr inbounds <16 x i8>, ptr %b, i64 %i
  %po = getelementptr inbounds <8 x i8>, ptr %o, i64 %i

  %la = load <16 x i8>, ptr %pa, align 16
  %lb = load <16 x i8>, ptr %pb, align 16
  %va = bitcast <16 x i8> %la to <64 x i2>
  %vb = bitcast <16 x i8> %lb to <64 x i2>

  %lt = icmp ult <64 x i2> %va, %vb

  %lo = bitcast <64 x i1> %lt to <8 x i8>
  store <8 x i8> %lo, ptr %po, align 8

  %i.next = add nuw i64 %i, 1
  %more = icmp ult i64 %i.next, %nvec
  br i1 %more, label %loop, label %done

done:
  ret void
}
