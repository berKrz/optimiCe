#include <criterion/criterion.h>
#include <criterion/new/assert.h>
#include <math.h>

#include "unittest.h"

#define MAX_N 40

// The expected order, by insertion sort: by fitness, ascending once multiplied by `sign`, then by
// index. sign is +1 for minimizing best-first and maximizing worst-first, -1 for the other two
static void reference_order(const double* fits, int n, double sign, Ranked* out) {
  for (int i = 0; i < n; i++) {
    Ranked r = {fits[i], i};
    int    j = i;
    for (; j > 0; j--) {
      double a = sign * out[j - 1].fitness, b = sign * r.fitness;
      if (a < b || (a == b && out[j - 1].index < r.index)) break;
      out[j] = out[j - 1];
    }
    out[j] = r;
  }
}

// Random arrays drawn from a few values, so ties are common, infinities included
Test(rank, matches_reference) {
  static const double pool[]        = {-INFINITY, -2.0, 0.0, 0.5, 3.0, INFINITY};
  static const char*  dir_names[]   = {"minimize", "maximize"};
  static const char*  order_names[] = {"best first", "worst first"};
  rng_init(1);

  for (int trial = 0; trial < 200; trial++) {
    int    n = rng_next_int(1, MAX_N + 1);
    double fits[MAX_N];
    for (int i = 0; i < n; i++) fits[i] = pool[rng_next_int(0, 6)];

    for (int d = 0; d < 2; d++)
      for (int o = 0; o < 2; o++) {
        g_cfg_rw.direction = d == 0 ? MINIMIZE : MAXIMIZE;
        RankOrder order    = o == 0 ? RANK_BEST_FIRST : RANK_WORST_FIRST;
        double    sign     = (d == 0) == (o == 0) ? 1.0 : -1.0;

        Ranked got[MAX_N], expected[MAX_N];
        int    seen[MAX_N] = {0};
        rank_by_fitness(fits, n, got, order);
        reference_order(fits, n, sign, expected);

        for (int i = 0; i < n; i++) {
          cr_assert(got[i].index >= 0 && got[i].index < n);
          cr_assert(eq(int, seen[got[i].index]++, 0), "index %d twice", got[i].index);
          cr_assert(eq(dbl, got[i].fitness, fits[got[i].index]));
          cr_assert(eq(int, got[i].index, expected[i].index), "trial %d, %s, %s, position %d",
                    trial, dir_names[d], order_names[o], i);
        }
      }
  }
}
