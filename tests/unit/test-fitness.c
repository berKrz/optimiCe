#include <criterion/criterion.h>
#include <criterion/new/assert.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "unittest.h"

// Evaluates `pos` with the catalog entry `name`, in n_dims dimensions
static double eval_at(const char* name, const double* pos, int n_dims) {
  g_cfg_rw.fitness = fitness_find(name);
  cr_assert(not(eq(ptr, (void*) g_cfg_rw.fitness, NULL)), "no fitness function '%s'", name);
  g_cfg_rw.n_dims = n_dims;
  return evaluate(pos);
}

// The same, with the point written out: EVAL("sphere", 1, 2, 3)
#define EVAL(name, ...)                         \
  eval_at(name, (const double[]) {__VA_ARGS__}, \
          (int) (sizeof((double[]) {__VA_ARGS__}) / sizeof(double)))

// Exact up to rounding in the function's own arithmetic
#define TOL 1e-12

// --- Known values ---

Test(fitness, sphere) {
  double zeros[30] = {0};
  cr_expect(eq(dbl, eval_at("sphere", zeros, 1), 0.0));
  cr_expect(eq(dbl, eval_at("sphere", zeros, 2), 0.0));
  cr_expect(eq(dbl, eval_at("sphere", zeros, 30), 0.0));
  cr_expect(eq(dbl, EVAL("sphere", 1, 2, 3), 14.0));
}

Test(fitness, rosenbrock) {
  double ones[10] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
  cr_expect(eq(dbl, eval_at("rosenbrock", ones, 2), 0.0));
  cr_expect(eq(dbl, eval_at("rosenbrock", ones, 10), 0.0));
  cr_expect(eq(dbl, EVAL("rosenbrock", 0, 0), 1.0));
  cr_expect(eq(dbl, EVAL("rosenbrock", -1, 1), 4.0));
}

Test(fitness, rastrigin) {
  cr_expect(epsilon_eq(dbl, EVAL("rastrigin", 0, 0, 0), 0.0, TOL));
  cr_expect(epsilon_eq(dbl, EVAL("rastrigin", 1, 1), 2.0, TOL));
  cr_expect(epsilon_eq(dbl, EVAL("rastrigin", 0.5), 20.25, TOL));
}

// At π/2 the product term is cos(π/2), 0
Test(fitness, griewank) {
  cr_expect(epsilon_eq(dbl, EVAL("griewank", 0, 0, 0), 0.0, TOL));
  cr_expect(epsilon_eq(dbl, EVAL("griewank", M_PI / 2), 1.0 + (M_PI * M_PI / 4) / 4000.0, TOL));
}

// At the origin, rounding leaves about 4.4e-16
Test(fitness, ackley) {
  cr_expect(epsilon_eq(dbl, EVAL("ackley", 0, 0, 0), 0.0, TOL));
  cr_expect(epsilon_eq(dbl, EVAL("ackley", 1, 1), 20.0 * (1.0 - exp(-0.2)), TOL));
}

// The known optimum is given to 4 decimals, so it's matched to 1e-4
Test(fitness, eggholder) {
  cr_expect(epsilon_eq(dbl, EVAL("eggholder", 512, 404.2319), -959.6407, 1e-4));
  cr_expect(epsilon_eq(dbl, EVAL("eggholder", 0, 0), -47.0 * sin(sqrt(47.0)), TOL));
}

Test(fitness, schwefel) {
  const double x = 420.9687463;
  cr_expect(epsilon_eq(dbl, EVAL("schwefel", x, x, x), 0.0, 3e-9));
  cr_expect(epsilon_eq(dbl, EVAL("schwefel", 0, 0, 0), 3 * 418.9828872724338, TOL));
}

Test(fitness, schaffer_f6) {
  cr_expect(epsilon_eq(dbl, EVAL("schaffer_f6", 0, 0), 0.0, TOL));
  double expected = 0.5 + (sin(1.0) * sin(1.0) - 0.5) / (1.001 * 1.001);
  cr_expect(epsilon_eq(dbl, EVAL("schaffer_f6", 1, 0), expected, TOL));
}

// --- The catalog ---

// No two entries share a name: a later one would never be found
Test(fitness, names_unique) {
  for (int i = 0; fitness_at(i); i++)
    for (int j = i + 1; fitness_at(j); j++)
      cr_expect(
          not(eq(str, (char*) fitness_name(fitness_at(i)), (char*) fitness_name(fitness_at(j)))));
}

// An entry's limits, read back from the text --list shows: domain "any" or "within [lo, hi]",
// n_dims "any", "N" or "N or more"
typedef struct {
  double lo, hi;
  int    dims_min, dims_fixed;
} Limits;

static Limits limits_of(const FitnessEntry* fe) {
  char   dims[FITNESS_LIMIT_LEN], domain[FITNESS_LIMIT_LEN];
  Limits l = {-INFINITY, INFINITY, 1, 0};
  fitness_limits_text(fe, dims, domain);
  if (strcmp(domain, "any") != 0)
    cr_assert(eq(int, sscanf(domain, "within [%lf, %lf]", &l.lo, &l.hi), 2), "domain '%s'", domain);
  if (strcmp(dims, "any") != 0) {
    cr_assert(eq(int, sscanf(dims, "%d", &l.dims_min), 1), "n_dims '%s'", dims);
    l.dims_fixed = strstr(dims, "or more") == NULL;
  }
  return l;
}

// Uniform in [lo, hi], with an infinite end replaced by ±scale
static double draw_in(double lo, double hi, double scale) {
  if (isinf(lo)) lo = isinf(hi) ? -scale : hi - scale;
  if (isinf(hi)) hi = lo + scale;
  return rng_next_double_range(lo, hi);
}

// The catalog's promise: no function returns NaN at a finite point of its domain, since nothing
// compares better or worse than NaN. Overflowing to an infinity is allowed
// Every entry is checked at its domain's corners and center and at random points, out to the
// largest doubles where the domain is unbounded
Test(fitness, never_nan_in_domain) {
  static const double scales[] = {1.0, 1e3, 1e10, 1e100, 1e150, 1e300, DBL_MAX};
  rng_init(1);

  for (int i = 0; fitness_at(i); i++) {
    const FitnessEntry* fe      = fitness_at(i);
    const char*         name    = fitness_name(fe);
    Limits              l       = limits_of(fe);
    int                 dims[2] = {l.dims_min > 2 ? l.dims_min : 2, 10};
    g_cfg_rw.fitness            = fe;

    for (int d = 0; d < (l.dims_fixed ? 1 : 2); d++) {
      int    n = dims[d];
      double pos[10];
      g_cfg_rw.n_dims = n;

      for (size_t s = 0; s < sizeof(scales) / sizeof(scales[0]); s++) {
        double lo = isinf(l.lo) ? -scales[s] : l.lo, hi = isinf(l.hi) ? scales[s] : l.hi;
        double corners[3] = {lo, hi, lo / 2 + hi / 2};
        for (int c = 0; c < 3; c++) {
          for (int k = 0; k < n; k++) pos[k] = corners[(c + k) % 3];
          double f = evaluate(pos);
          cr_assert(isnan(f) == 0, "%s, %d dims: NaN at corner %d, scale %g", name, n, c,
                    scales[s]);
        }
        for (int p = 0; p < 1000; p++) {
          for (int k = 0; k < n; k++) pos[k] = draw_in(l.lo, l.hi, scales[s]);
          double f = evaluate(pos);
          cr_assert(isnan(f) == 0, "%s, %d dims: NaN at scale %g, e.g. pos[0] = %g", name, n,
                    scales[s], pos[0]);
        }
      }
    }
  }
}
