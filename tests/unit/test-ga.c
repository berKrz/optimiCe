#include <criterion/criterion.h>
#include <criterion/new/assert.h>
#include <stdint.h>
#include <stdlib.h>

#include "helpers.h"

static void set_domain(double lo, double hi) {
  g_cfg_rw.domain_min = lo;
  g_cfg_rw.domain_max = hi;
  g_cfg_rw.init_min   = lo;
  g_cfg_rw.init_max   = hi;
}

static void setup(void) {
  set_defaults(algo_named("ga")->fields, algo_named("ga")->cfg_base());
  g_cfg_rw.direction = MINIMIZE;
  g_cfg_rw.n_dims    = 1;
  set_domain(-5.0, 5.0);
}

TestSuite(ga, .init = setup);

#define IND_SIZE (*(int*) live_field("ga", "ind_size"))

static uint64_t max_level_of(int ind_size) { return (UINT64_C(1) << ind_size) - 1; }

// Writes `level` into dimension d's bits: bit b of dimension d is bit d * ind_size + b of the
// individual, least significant first, 64 to a word
static void write_level(uint64_t* ind, int d, int ind_size, uint64_t level) {
  for (int b = 0; b < ind_size; b++) {
    int i = d * ind_size + b;
    if ((level >> b) & 1) ind[i / 64] |= UINT64_C(1) << (i % 64);
  }
}

// --- Encoding ---

// Level 0 is exactly domain_min and the last level exactly domain_max
Test(ga, level_value_ends) {
  const double domains[][2] = {
      {-5.0,   5.0  },
      {-5.12,  5.12 },
      {-600.0, 600.0},
      {0.1,    0.7  }
  };
  const int sizes[] = {2, 22, 63};
  for (int k = 0; k < 4; k++)
    for (int s = 0; s < 3; s++) {
      set_domain(domains[k][0], domains[k][1]);
      IND_SIZE = sizes[s];
      cr_expect(eq(dbl, level_value(0), domains[k][0]), "ind_size %d", sizes[s]);
      cr_expect(eq(dbl, level_value(max_level_of(sizes[s])), domains[k][1]), "ind_size %d",
                sizes[s]);
    }
}

Test(ga, init_levels_whole_domain) {
  IND_SIZE    = 22;
  uint64_t lo = 1, hi = 0;
  cr_assert(eq(int, init_levels(&lo, &hi), 1));
  cr_expect(eq(u64, lo, 0));
  cr_expect(eq(u64, hi, max_level_of(22)));
}

// Exactly the levels inside: [lo, hi] decodes inside the init range, lo - 1 and hi + 1 outside
static void check_init_levels(double init_min, double init_max) {
  g_cfg_rw.init_min = init_min;
  g_cfg_rw.init_max = init_max;
  uint64_t lo, hi;
  cr_assert(eq(int, init_levels(&lo, &hi), 1), "[%g, %g]", init_min, init_max);
  cr_expect(level_value(lo) >= init_min, "[%g, %g]: lo", init_min, init_max);
  cr_expect(level_value(hi) <= init_max, "[%g, %g]: hi", init_min, init_max);
  if (lo > 0) cr_expect(level_value(lo - 1) < init_min, "[%g, %g]: lo - 1", init_min, init_max);
  if (hi < max_level_of(IND_SIZE))
    cr_expect(level_value(hi + 1) > init_max, "[%g, %g]: hi + 1", init_min, init_max);
}

Test(ga, init_levels_narrower_range) {
  IND_SIZE = 8;
  check_init_levels(-1.0, 2.5);
  check_init_levels(-5.0, -4.9);
  check_init_levels(4.99, 5.0);
  check_init_levels(level_value(37), level_value(200));  // ends exactly on levels
}

// Levels on [-5, 5] with 2 bits: -5, -5/3, 5/3, 5; [0, 1] holds none
Test(ga, init_levels_none_inside) {
  IND_SIZE          = 2;
  g_cfg_rw.init_min = 0.0;
  g_cfg_rw.init_max = 1.0;
  uint64_t lo, hi;
  cr_expect(eq(int, init_levels(&lo, &hi), 0));
}

// Four 22-bit dimensions take 88 bits: dimension 2 spans the two words
Test(ga, decode_dim_packing) {
  IND_SIZE                = 22;
  g_cfg_rw.n_dims         = 4;
  const uint64_t levels[] = {0, max_level_of(22), 0x2AAAAA, 1234567};
  uint64_t       ind[2]   = {0};
  for (int d = 0; d < 4; d++) write_level(ind, d, 22, levels[d]);
  for (int d = 0; d < 4; d++)
    cr_expect(eq(dbl, decode_dim(ind, d), level_value(levels[d])), "dim %d", d);
}

// --- Crossover ---

static void set_cuts(int n, const double* v) {
  DoubleList* cuts = live_field("ga", "cut_points");
  cuts->n          = n;
  for (int i = 0; i < n; i++) cuts->v[i] = v[i];
}

// Two 8-bit dimensions, all-0 and all-1 parents. One cut at 0.5 (bit 4): bits 4-7 of each
// dimension are exchanged
Test(ga, crossover_one_cut) {
  IND_SIZE        = 8;
  g_cfg_rw.n_dims = 2;
  set_cuts(1, (const double[]){0.5});
  uint64_t a = 0, b = 0xFFFF;
  crossover_n_point(&a, &b);
  cr_expect(eq(u64, a, 0xF0F0));
  cr_expect(eq(u64, b, 0x0F0F));
}

// Cuts at 0.25 and 0.75 (bits 2 and 6): keep 0-1, exchange 2-5, keep 6-7
Test(ga, crossover_two_cuts) {
  IND_SIZE        = 8;
  g_cfg_rw.n_dims = 2;
  set_cuts(2, (const double[]){0.25, 0.75});
  uint64_t a = 0, b = 0xFFFF;
  crossover_n_point(&a, &b);
  cr_expect(eq(u64, a, 0x3C3C));
  cr_expect(eq(u64, b, 0xC3C3));
}

// Each bit position keeps the parents' pair of values: only which child holds which may change
Test(ga, crossover_uniform_exchanges_only) {
  IND_SIZE        = 22;
  g_cfg_rw.n_dims = 3;  // 66 bits, two words
  rng_init(5);
  for (int trial = 0; trial < 100; trial++) {
    uint64_t a[2], b[2];
    for (int w = 0; w < 2; w++) {
      a[w] = rng_next_uint64(UINT64_MAX);
      b[w] = rng_next_uint64(UINT64_MAX);
    }
    a[1] &= 3, b[1] &= 3;  // only the 66 bits in use
    uint64_t both[2] = {a[0] & b[0], a[1] & b[1]}, either[2] = {a[0] | b[0], a[1] | b[1]};
    crossover_uniform(a, b);
    for (int w = 0; w < 2; w++) {
      cr_expect(eq(u64, a[w] & b[w], both[w]), "trial %d, word %d", trial, w);
      cr_expect(eq(u64, a[w] | b[w], either[w]), "trial %d, word %d", trial, w);
    }
  }
}

// --- Selection weights ---

static void expect_weights(const double* got, const double* expected, int n, const char* what) {
  for (int i = 0; i < n; i++)
    cr_expect(epsilon_eq(dbl, got[i], expected[i], 1e-12), "%s, individual %d", what, i);
}

// Shifted past the worst: worst gets 1, best range + 1
Test(ga, weights_roulette) {
  const double fits[] = {3.0, 1.0, 2.0}, equal[] = {2.0, 2.0, 2.0};
  double       w[3];
  weights_roulette(fits, w, 3);
  expect_weights(w, (const double[]){1.0, 3.0, 2.0}, 3, "minimize");
  g_cfg_rw.direction = MAXIMIZE;
  weights_roulette(fits, w, 3);
  expect_weights(w, (const double[]){3.0, 1.0, 2.0}, 3, "maximize");
  weights_roulette(equal, w, 3);
  expect_weights(w, (const double[]){1.0, 1.0, 1.0}, 3, "equal");
}

// The worst gets 1 / (n - 1), the best 1 + 1 / (n - 1); all 1 when every fitness is equal
// Here the range is 4 and n = 3, so the shift past the worst is 2
Test(ga, weights_roulette_normalized) {
  const double fits[] = {5.0, 1.0, 3.0}, equal[] = {2.0, 2.0, 2.0};
  double       w[3];
  weights_roulette_normalized(fits, w, 3);
  expect_weights(w, (const double[]){0.5, 1.5, 1.0}, 3, "minimize");
  g_cfg_rw.direction = MAXIMIZE;
  weights_roulette_normalized(fits, w, 3);
  expect_weights(w, (const double[]){1.5, 0.5, 1.0}, 3, "maximize");
  weights_roulette_normalized(equal, w, 3);
  expect_weights(w, (const double[]){1.0, 1.0, 1.0}, 3, "equal");
}

// By rank alone, worst to best: 2 - s up to s, averaging 1; the tie goes by index
Test(ga, weights_rank) {
  *(double*) live_field("ga", "rank_pressure") = 1.5;
  const double fits[]                          = {3.0, 1.0, 2.0, 2.0};
  double       w[4], sum = 0.0;
  Ranked       order[4];
  weights_rank(fits, w, order, 4);
  expect_weights(w, (const double[]){0.5, 1.5, 0.5 + 1.0 / 3.0, 0.5 + 2.0 / 3.0}, 4, "minimize");
  for (int i = 0; i < 4; i++) sum += w[i];
  cr_expect(epsilon_eq(dbl, sum / 4, 1.0, 1e-12));
}

Test(ga, best_of) {
  const double fits[] = {2.0, 1.0, 1.0, 3.0};
  cr_expect(eq(int, best_of(fits, 4, -1), 1));  // tie: the earliest
  cr_expect(eq(int, best_of(fits, 4, 1), 2));
  g_cfg_rw.direction = MAXIMIZE;
  cr_expect(eq(int, best_of(fits, 4, -1), 3));
  cr_expect(eq(int, best_of(fits, 4, 3), 0));
}

// --- Evaluation count ---

// One evaluation per individual at init, then one per child per generation done, with or
// without elitism, for an odd or even population
static void check_count(const char* extra) {
  char toml[512];
  snprintf(toml, sizeof(toml),
           "[common]\nseed = 3\niterations = 25\n[[run]]\nalgorithm = \"ga\"\n%s", extra);
  Batch     b   = load_toml(toml);
  RunResult res = run_job_of(&b, 0);
  uint64_t  n   = (uint64_t) * (int*) algo_field(&b.runs[0], "pop_size");
  cr_expect(eq(u64, res.evaluations, n + n * res.stop.iterations), "%s", extra);
  free(res.pos);
  free_batch(&b);
}

Test(ga, evaluation_count) {
  check_count("elitism = \"none\"\npop_size = 15\n");
  check_count("elitism = \"none\"\npop_size = 16\n");
  check_count("elitism = \"family\"\npop_size = 15\n");
  check_count("elitism = \"family\"\npop_size = 16\n");
  check_count("elitism = \"family\"\npop_size = 15\ntarget_fitness = 1e300\n");
}
