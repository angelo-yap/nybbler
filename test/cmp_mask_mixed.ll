; Fallback guard for the compare mask fold (companion to cmp_mask_fold.ll).
;
; The fold hands the field mask to `sext` users and skips building the <K x i1>.
; When a compare has users *beyond* those sexts, the boolean vector is genuinely
; needed and the trunc must still be emitted -- the fold rewrites the mask users
; it can and leaves the rest on the ordinary path. This asserts both halves
; happen for one compare feeding both a sext and a select.
; RUN: %python "%diff_runner" --opt "%opt" --lli "%lli" --plugin "%nybbler" "%s" | %FileCheck "%s"
; RUN: %opt -load-pass-plugin "%nybbler" -passes=nybbler "%s" -S | %FileCheck --check-prefix=SHAPE "%s"
; CHECK: ALL PASS
; SHAPE-LABEL: @mixed_users_i4
; SHAPE: %cmp.trunc = trunc <32 x i4> %{{.*}} to <32 x i1>
; SHAPE: select <32 x i1> %cmp.trunc
; SHAPE-NOT: = sext

define <32 x i4> @mixed_users_i4(<32 x i4> %a, <32 x i4> %b) {
  %c = icmp eq <32 x i4> %a, %b
  %m = sext <32 x i1> %c to <32 x i4>
  %s = select <32 x i1> %c, <32 x i4> %m, <32 x i4> %a
  ret <32 x i4> %s
}
