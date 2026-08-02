; cmp_i4 -- unsigned per-field compare to a packed bitmask.
;
; NYB_DESC: <32 x i4> icmp ult -> packed <32 x i1> bitmask  (PARTIAL, see below)
; NYB_OUT_BYTES_PER_VEC: 4
;
; PARTIAL RESULT -- read the number with the caveat. nybbler lowers the `icmp`
; itself, but not the `bitcast <32 x i1> to <4 x i8>` that materializes its
; result into a packed mask. That materialization is expensive in *both*
; builds and dominates the loop, so the measured ratio understates the
; compare lowering considerably. There is no way to fix this from the kernel
; side: any consumer of a <K x i1> result (bitcast, sext, select) is outside
; the set of instructions the pass rewrites. See the Limitations section of
; docs/benchmarks.md.
;
; It is kept in the suite anyway because compare-to-bitmask is the actual
; Parabix-style idiom, and an honest partial number is more useful than
; silently omitting the compare lowering from the benchmark set.

define void @nyb_kernel(ptr noalias %a, ptr noalias %b, ptr noalias %o, i64 %nvec) {
entry:
  %nonempty = icmp ugt i64 %nvec, 0
  br i1 %nonempty, label %loop, label %done

loop:
  %i = phi i64 [ 0, %entry ], [ %i.next, %loop ]
  %pa = getelementptr inbounds <16 x i8>, ptr %a, i64 %i
  %pb = getelementptr inbounds <16 x i8>, ptr %b, i64 %i
  %po = getelementptr inbounds <4 x i8>, ptr %o, i64 %i

  %la = load <16 x i8>, ptr %pa, align 16
  %lb = load <16 x i8>, ptr %pb, align 16
  %va = bitcast <16 x i8> %la to <32 x i4>
  %vb = bitcast <16 x i8> %lb to <32 x i4>

  %lt = icmp ult <32 x i4> %va, %vb

  %lo = bitcast <32 x i1> %lt to <4 x i8>
  store <4 x i8> %lo, ptr %po, align 4

  %i.next = add nuw i64 %i, 1
  %more = icmp ult i64 %i.next, %nvec
  br i1 %more, label %loop, label %done

done:
  ret void
}
