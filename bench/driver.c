/* driver.c -- timing and checksum harness for the nybbler benchmark kernels.
 *
 * Kernel-agnostic: every kernel in bench/kernels/ exports the same symbol with
 * the same signature, so this file is compiled once per kernel and linked
 * against either the baseline object (llc alone, narrow op scalarized) or the
 * lowered object (opt -passes=nybbler, then the identical llc). The two
 * binaries differ only by that pass, which is what makes the comparison fair.
 *
 * Build-time knob (run.sh reads it from the kernel's NYB_OUT_BYTES_PER_VEC
 * comment): kernels that write a packed compare mask produce fewer output
 * bytes than they consume.
 *
 * Usage:
 *   ./bench_x               time it and print a result line
 *   ./bench_x --check-only  print only CHECKSUM <hex>, no timing
 */

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

/* Input vectors are always 16 bytes: the carrier nybbler bitcasts to is
 * <16 x i8> for every width (128 bits = 32 x i4 = 64 x i2 = 128 x i1). */
#define VEC_BYTES 16

#ifndef NYB_OUT_BYTES_PER_VEC
#define NYB_OUT_BYTES_PER_VEC 16
#endif

#ifndef NYB_NAME
#define NYB_NAME "kernel"
#endif

/* 256 KiB per buffer. Sized to stay resident in the 512 KiB per-core L2 of the
 * development machine so the loop is compute-bound: at DRAM bandwidth both
 * builds would converge on the memory system and the lowering difference would
 * disappear into the noise. */
#define BUF_BYTES (256u * 1024u)
/* Exact: 262144 / 16. clang-tidy flags this as integer division reaching a
 * floating-point context below; it divides evenly by construction. */
#define NVEC      (BUF_BYTES / VEC_BYTES)

#define WARMUP_REPS 20
#define TIMED_REPS  50

void nyb_kernel(const uint8_t *a, const uint8_t *b, uint8_t *o, uint64_t nvec);

/* Deterministic fill. A fixed-seed xorshift64 rather than rand() so both
 * builds see byte-identical inputs and the checksum is reproducible across
 * machines and libc versions. */
static void fill(uint8_t *p, size_t n, uint64_t seed) {
    uint64_t s = seed;
    for (size_t i = 0; i < n; i++) {
        s ^= s << 13;
        s ^= s >> 7;
        s ^= s << 17;
        p[i] = (uint8_t)(s >> 24);
    }
}

/* FNV-1a over the output buffer. Two jobs: it is the correctness gate that
 * run.sh compares between the two builds, and it makes the output live so the
 * optimizer cannot delete the kernel loop as dead. */
static uint64_t checksum(const uint8_t *p, size_t n) {
    uint64_t h = 1469598103934665603ULL;
    for (size_t i = 0; i < n; i++) {
        h ^= p[i];
        h *= 1099511628211ULL;
    }
    return h;
}

static double now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1e9 + (double)ts.tv_nsec;
}

static int cmp_double(const void *x, const void *y) {
    double a = *(const double *)x, b = *(const double *)y;
    return (a > b) - (a < b);
}

int main(int argc, char **argv) {
    int check_only = (argc > 1 && strcmp(argv[1], "--check-only") == 0);

    size_t out_bytes = (size_t)NVEC * NYB_OUT_BYTES_PER_VEC;

    uint8_t *a = aligned_alloc(64, BUF_BYTES);
    uint8_t *b = aligned_alloc(64, BUF_BYTES);
    uint8_t *o = aligned_alloc(64, out_bytes);
    if (!a || !b || !o) {
        fprintf(stderr, "allocation failed\n");
        return 1;
    }

    fill(a, BUF_BYTES, 0x9E3779B97F4A7C15ULL);
    fill(b, BUF_BYTES, 0xD1B54A32D192ED03ULL);
    memset(o, 0, out_bytes);

    for (int r = 0; r < WARMUP_REPS; r++)
        nyb_kernel(a, b, o, NVEC);

    if (check_only) {
        printf("CHECKSUM %016llx\n", (unsigned long long)checksum(o, out_bytes));
        free(a); free(b); free(o);
        return 0;
    }

    /* Time each repetition separately and report the minimum. The minimum is
     * the least contaminated sample: scheduling and interrupts can only ever
     * add time, never remove it. The median is printed alongside so a run
     * where the machine was busy throughout is visible rather than hidden. */
    double samples[TIMED_REPS];
    for (int r = 0; r < TIMED_REPS; r++) {
        double t0 = now_ns();
        nyb_kernel(a, b, o, NVEC);
        samples[r] = now_ns() - t0;
    }

    uint64_t sum = checksum(o, out_bytes);

    qsort(samples, TIMED_REPS, sizeof(double), cmp_double);
    double best = samples[0];
    double median = samples[TIMED_REPS / 2];

    double ns_per_vec = best / (double)NVEC;
    double gib_per_s = ((double)BUF_BYTES / (best * 1e-9)) / (1024.0 * 1024.0 * 1024.0);

    /* One machine-parsable line; run.sh splits on whitespace. */
    printf("RESULT %s ns_per_vec=%.4f median_ns_per_vec=%.4f gib_per_s=%.2f "
           "checksum=%016llx nvec=%u\n",
           NYB_NAME, ns_per_vec, median / (double)NVEC, gib_per_s,
           (unsigned long long)sum, (unsigned)NVEC);

    free(a); free(b); free(o);
    return 0;
}
