#include <criterion/criterion.h>
#include <criterion/new/assert.h>
#include <pthread.h>
#include <stdint.h>

#include "unittest.h"

// --- Reference values ---

// Output of the reference splitmix64.c from a state of 0
static const uint64_t splitmix64_from_zero[] = {
    0xe220a8397b1dcdafULL,
    0x6e789e6aa1b965f4ULL,
    0x06c45d188009454fULL,
};

// Output of the reference xoshiro256starstar.c, its state filled with four
// splitmix64 outputs from 42, as rng_init(42) does
static const uint64_t xoshiro_from_42[] = {
    0x15780b2e0c2ec716ULL, 0x6104d9866d113a7eULL, 0xae17533239e499a1ULL,
    0xecb8ad4703b360a1ULL, 0xfde6dc7fe2ec5e64ULL,
};

#define COUNT(a) (sizeof(a) / sizeof((a)[0]))

// rng_next_double's mapping of a 64-bit output: its top 53 bits, scaled to [0, 1)
static double to_unit(uint64_t x) { return (double) (x >> 11) * 0x1.0p-53; }

// --- Generators ---

Test(rng, splitmix64_matches_reference) {
  uint64_t x = 0;
  for (size_t i = 0; i < COUNT(splitmix64_from_zero); i++)
    cr_expect(eq(u64, splitmix64(&x), splitmix64_from_zero[i]), "output %zu", i);
}

Test(rng, init_and_draw_match_reference) {
  rng_init(42);
  for (size_t i = 0; i < COUNT(xoshiro_from_42); i++)
    cr_expect(eq(dbl, rng_next_double(), to_unit(xoshiro_from_42[i])), "draw %zu", i);
}

// --- Reproducibility ---

#define STREAM_LEN 1000

static void draw_stream(uint32_t seed, double* out) {
  rng_init(seed);
  for (int i = 0; i < STREAM_LEN; i++) out[i] = rng_next_double();
}

Test(rng, same_seed_same_stream) {
  double a[STREAM_LEN], b[STREAM_LEN];
  draw_stream(7, a);
  draw_stream(7, b);
  for (int i = 0; i < STREAM_LEN; i++) cr_assert(eq(dbl, a[i], b[i]), "draw %d", i);
}

Test(rng, different_seeds_different_streams) {
  double a[STREAM_LEN], b[STREAM_LEN];
  draw_stream(7, a);
  draw_stream(8, b);
  int same = 0;
  for (int i = 0; i < STREAM_LEN; i++) same += a[i] == b[i];
  cr_expect(eq(int, same, 0));
}

static void* draw_in_thread(void* out) {
  draw_stream(9, out);
  return NULL;
}

// Every thread has its own stream: two threads seeded alike draw the same numbers as this thread
// would, and seeding them doesn't move this thread's stream
Test(rng, streams_are_per_thread) {
  double expected[STREAM_LEN];
  draw_stream(9, expected);

  rng_init(5);
  double before = rng_next_double();

  static double streams[2][STREAM_LEN];
  pthread_t     threads[2];
  for (int t = 0; t < 2; t++)
    cr_assert(eq(int, pthread_create(&threads[t], NULL, draw_in_thread, streams[t]), 0));
  for (int t = 0; t < 2; t++) pthread_join(threads[t], NULL);

  double after = rng_next_double();

  for (int t = 0; t < 2; t++)
    for (int i = 0; i < STREAM_LEN; i++)
      cr_assert(eq(dbl, streams[t][i], expected[i]), "thread %d, draw %d", t, i);

  rng_init(5);
  cr_expect(eq(dbl, rng_next_double(), before));
  cr_expect(eq(dbl, rng_next_double(), after));
}

// --- Ranges ---

#define DRAWS 100000

Test(rng, next_double_in_unit_interval) {
  rng_init(1);
  for (int i = 0; i < DRAWS; i++) {
    double x = rng_next_double();
    cr_assert(x >= 0.0 && x < 1.0, "draw %d: %a", i, x);
  }
}

Test(rng, next_double_range_in_range) {
  rng_init(2);
  for (int i = 0; i < DRAWS; i++) {
    double x = rng_next_double_range(-5.12, 5.12);
    cr_assert(x >= -5.12 && x < 5.12, "draw %d: %a", i, x);
  }
}

// Half-open, and every value in the range comes up
Test(rng, next_int_covers_range) {
  int seen[7] = {0};
  rng_init(3);
  for (int i = 0; i < DRAWS; i++) {
    int x = rng_next_int(-3, 4);
    cr_assert(x >= -3 && x < 4, "draw %d: %d", i, x);
    seen[x + 3] = 1;
  }
  for (int v = 0; v < 7; v++) cr_expect(eq(int, seen[v], 1), "value %d never drawn", v - 3);
}

Test(rng, next_uint64_covers_range) {
  int seen[10] = {0};
  rng_init(4);
  for (int i = 0; i < DRAWS; i++) {
    uint64_t x = rng_next_uint64(10);
    cr_assert(lt(u64, x, 10), "draw %d", i);
    seen[x] = 1;
  }
  for (int v = 0; v < 10; v++) cr_expect(eq(int, seen[v], 1), "value %d never drawn", v);
}

Test(rng, next_uint64_of_one_is_zero) {
  rng_init(5);
  for (int i = 0; i < 100; i++) cr_assert(eq(u64, rng_next_uint64(1), 0));
}

// Just above 2^63, about half of the raw outputs are rejected and retried
Test(rng, next_uint64_large_bound) {
  uint64_t n = (UINT64_C(1) << 63) + 1;
  rng_init(6);
  for (int i = 0; i < DRAWS; i++) cr_assert(lt(u64, rng_next_uint64(n), n), "draw %d", i);
}

Test(rng, next_bit_draws_both) {
  int seen[2] = {0};
  rng_init(7);
  for (int i = 0; i < 1000; i++) {
    int b = rng_next_bit();
    cr_assert(b == 0 || b == 1, "draw %d: %d", i, b);
    seen[b] = 1;
  }
  cr_expect(seen[0] && seen[1]);
}
