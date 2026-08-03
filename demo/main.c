// Driver for the nybbler end-to-end demo.
//
// Calls the lowered narrow-field kernels on real buffers and checks every
// nibble against a plain scalar reference computed here in C. The point is
// that the SWAR lowering is not just fast-looking assembly -- it computes the
// same per-field results the source IR asked for.

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

void nibble_add(const void *a, const void *b, void *out, uint64_t nchunks);
void nibble_lt_mask(const void *a, const void *b, void *out, uint64_t nchunks);

#define CHUNKS 4
#define NBYTES (CHUNKS * 16)

static uint8_t lo(uint8_t v) { return v & 0xF; }
static uint8_t hi(uint8_t v) { return v >> 4; }

// Reference: pack two independent 4-bit results back into a byte.
static uint8_t pack(uint8_t lo_val, uint8_t hi_val) {
  return (uint8_t)((lo_val & 0xF) | ((hi_val & 0xF) << 4));
}

int main(void) {
  uint8_t a[NBYTES], b[NBYTES], got[NBYTES];
  int failures = 0;

  // Deterministic but varied, so every nibble value and every carry case out
  // of a 4-bit field is exercised somewhere in the buffer.
  for (int i = 0; i < NBYTES; i++) {
    a[i] = (uint8_t)(i * 7 + 3);
    b[i] = (uint8_t)(i * 13 + 11);
  }

  nibble_add(a, b, got, CHUNKS);
  for (int i = 0; i < NBYTES; i++) {
    uint8_t want = pack(lo(a[i]) + lo(b[i]), hi(a[i]) + hi(b[i]));
    if (got[i] != want) {
      if (failures < 5)
        printf("  nibble_add[%d]: got %02x want %02x\n", i, got[i], want);
      failures++;
    }
  }

  nibble_lt_mask(a, b, got, CHUNKS);
  for (int i = 0; i < NBYTES; i++) {
    uint8_t want = pack(lo(a[i]) < lo(b[i]) ? 0xF : 0x0,
                        hi(a[i]) < hi(b[i]) ? 0xF : 0x0);
    if (got[i] != want) {
      if (failures < 5)
        printf("  nibble_lt_mask[%d]: got %02x want %02x\n", i, got[i], want);
      failures++;
    }
  }

  if (failures) {
    printf("DEMO FAILED: %d of %d nibble pairs wrong\n", failures, NBYTES * 2);
    return 1;
  }
  printf("DEMO OK: %d nibbles per kernel match the scalar reference\n",
         NBYTES * 2);
  return 0;
}
