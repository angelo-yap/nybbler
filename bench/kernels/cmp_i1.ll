; cmp_i1 -- 1-bit field equality compare to a packed bitmask.
;
; NYB_DESC: <128 x i1> icmp eq -> packed <16 x i8> bitmask  (PARTIAL, see below)
; NYB_OUT_BYTES_PER_VEC: 16
;
; Uses icmp eq rather than ult deliberately: the static instruction-count
; data (tools/codegen_check.py --report) shows eq@i1 is the one real win at
; this width (7 vs 536 nybbler vs baseline instructions, 76.6x) while
; ne/ult/slt@i1 are flat 1.0x -- see README.md's note that eq is "the
; exception" to i1 degenerating. Using ult here would just reproduce the
; degenerate case that arith_i1.ll and shift_i1.ll already cover.
;
; PARTIAL RESULT, same caveat as cmp_i4 -- read with it. nybbler lowers the
; icmp itself, but not the `bitcast <128 x i1> to <16 x i8>` that
; materializes its result into a packed mask; that materialization is
; expensive in both builds and can dominate the loop, understating the
; compare lowering. See the Limitations section of docs/benchmarks.md.

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

  %eq = icmp eq <128 x i1> %va, %vb

  %lo = bitcast <128 x i1> %eq to <16 x i8>
  store <16 x i8> %lo, ptr %po, align 16

  %i.next = add nuw i64 %i, 1
  %more = icmp ult i64 %i.next, %nvec
  br i1 %more, label %loop, label %done

done:
  ret void
}
