#include <criterion/criterion.h>
#include <criterion/new/assert.h>
#include <math.h>

#include "unittest.h"

static void set_domain(double lo, double hi) {
  g_cfg_rw.domain_min = lo;
  g_cfg_rw.domain_max = hi;
  g_cfg_rw.init_min   = lo;
  g_cfg_rw.init_max   = hi;
}

static void setup(void) { set_domain(-1.0, 1.0); }

TestSuite(bounds, .init = setup);

static const BoundsMode modes[] = {BOUNDS_CLAMP, BOUNDS_REFLECT, BOUNDS_RANDOM};
#define N_MODES (int) (sizeof(modes) / sizeof(modes[0]))

// --- Inside the domain ---

// Untouched in every mode, bounds included, and random mode draws nothing from the RNG
Test(bounds, inside_is_untouched) {
  const double inside[] = {-1.0, 1.0, 0.0, 0.3};
  for (int m = 0; m < N_MODES; m++) {
    rng_init(3);
    for (int i = 0; i < 4; i++) {
      double x = inside[i], v = 0.7;
      apply_bounds(&x, &v, modes[m]);
      cr_expect(eq(dbl, x, inside[i]), "mode %d", m);
      cr_expect(eq(dbl, v, 0.7), "mode %d", m);
    }
    double next = rng_next_double();
    rng_init(3);
    cr_expect(eq(dbl, next, rng_next_double()), "mode %d drew from the RNG", m);
  }
}

// --- clamp ---

Test(bounds, clamp_pins_to_crossed_bound) {
  double x = -1.5, v = 2.0;
  apply_bounds(&x, &v, BOUNDS_CLAMP);
  cr_expect(eq(dbl, x, -1.0));
  cr_expect(eq(dbl, v, 0.0));

  x = 3.0, v = 2.0;
  apply_bounds(&x, &v, BOUNDS_CLAMP);
  cr_expect(eq(dbl, x, 1.0));
  cr_expect(eq(dbl, v, 0.0));
}

// --- reflect, finite domain ---

Test(bounds, reflect_one_bounce) {
  set_domain(2.0, 5.0);
  double x = 5.5, v = 1.0;
  apply_bounds(&x, &v, BOUNDS_REFLECT);
  cr_expect(eq(dbl, x, 4.5));
  cr_expect(eq(dbl, v, -1.0));

  x = 1.25, v = -1.0;
  apply_bounds(&x, &v, BOUNDS_REFLECT);
  cr_expect(eq(dbl, x, 2.75));
  cr_expect(eq(dbl, v, 1.0));
}

// Across the whole domain and back: same direction of travel
Test(bounds, reflect_two_bounces) {
  set_domain(2.0, 5.0);
  double x = 8.5, v = 1.0;
  apply_bounds(&x, &v, BOUNDS_REFLECT);
  cr_expect(eq(dbl, x, 2.5));
  cr_expect(eq(dbl, v, 1.0));

  x = -1.5, v = -1.0;
  apply_bounds(&x, &v, BOUNDS_REFLECT);
  cr_expect(eq(dbl, x, 4.5));
  cr_expect(eq(dbl, v, -1.0));
}

// 7.5 widths below lo: an even number of walls crossed, landing mid-domain
Test(bounds, reflect_many_widths) {
  double x = -16.0, v = 1.0;
  apply_bounds(&x, &v, BOUNDS_REFLECT);
  cr_expect(eq(dbl, x, 0.0));
  cr_expect(eq(dbl, v, 1.0));
}

// Against a triangle wave: x - lo folded into [0, 2 width), and the second half mirrored, which is
// also where the direction of travel is reversed
Test(bounds, reflect_sweep) {
  const double lo = 2.0, hi = 5.0, width = hi - lo;
  set_domain(lo, hi);
  for (double start = lo - 10 * width; start < hi + 10 * width; start += 0.37) {
    if (start >= lo && start <= hi) continue;
    double x = start, v = 1.0;
    apply_bounds(&x, &v, BOUNDS_REFLECT);

    double u = fmod(start - lo, 2 * width);
    if (u < 0) u += 2 * width;
    int    reversed = u > width;
    double expected = lo + (reversed ? 2 * width - u : u);

    cr_assert(x >= lo && x <= hi, "from %g: landed at %g", start, x);
    cr_expect(epsilon_eq(dbl, x, expected, 1e-9), "from %g", start);
    cr_expect(eq(dbl, v, reversed ? -1.0 : 1.0), "from %g", start);
  }
}

// So far out that the arithmetic can only roughly land: the result is still inside
Test(bounds, reflect_huge_stays_inside) {
  const double far[] = {1e300, -1e300, 1e17 + 0.5};
  for (int i = 0; i < 3; i++) {
    double x = far[i];
    apply_bounds(&x, NULL, BOUNDS_REFLECT);
    cr_expect(x >= -1.0 && x <= 1.0, "from %g: landed at %g", far[i], x);
  }
}

// --- reflect, a single wall ---

Test(bounds, reflect_single_wall) {
  set_domain(0.0, INFINITY);
  double x = -3.0, v = -2.0;
  apply_bounds(&x, &v, BOUNDS_REFLECT);
  cr_expect(eq(dbl, x, 3.0));
  cr_expect(eq(dbl, v, 2.0));

  set_domain(-INFINITY, 0.0);
  x = 3.0, v = 2.0;
  apply_bounds(&x, &v, BOUNDS_REFLECT);
  cr_expect(eq(dbl, x, -3.0));
  cr_expect(eq(dbl, v, -2.0));
}

// The mirror image would be past the largest double: lands on the wall
Test(bounds, reflect_mirror_overflow) {
  set_domain(1e308, INFINITY);
  double x = -1e308, v = -1.0;
  apply_bounds(&x, &v, BOUNDS_REFLECT);
  cr_expect(eq(dbl, x, 1e308));
  cr_expect(eq(dbl, v, 1.0));
}

// --- random ---

Test(bounds, random_redraws_in_domain) {
  rng_init(4);
  for (int i = 0; i < 1000; i++) {
    double x = i % 2 ? 5.0 : -5.0, v = 2.0;
    apply_bounds(&x, &v, BOUNDS_RANDOM);
    cr_assert(x >= -1.0 && x < 1.0, "draw %d: %g", i, x);
    cr_assert(eq(dbl, v, 0.0));
  }
}

// --- Non-finite coordinates ---

// Pinned to the nearest finite limit, in every mode: the domain's, or the init range's where the
// domain is unbounded. NaN goes to the upper one
static void check_pinned(double lo_expected, double hi_expected) {
  const double in[]       = {INFINITY, -INFINITY, NAN};
  const double expected[] = {hi_expected, lo_expected, hi_expected};
  for (int m = 0; m < N_MODES; m++)
    for (int i = 0; i < 3; i++) {
      double x = in[i], v = 1.0;
      apply_bounds(&x, &v, modes[m]);
      cr_expect(eq(dbl, x, expected[i]), "mode %d, input %g", m, in[i]);
      cr_expect(eq(dbl, v, 0.0), "mode %d, input %g", m, in[i]);
    }
}

Test(bounds, non_finite_finite_domain) { check_pinned(-1.0, 1.0); }

Test(bounds, non_finite_unbounded_domain) {
  set_domain(-INFINITY, INFINITY);
  g_cfg_rw.init_min = -3.0;
  g_cfg_rw.init_max = 4.0;
  check_pinned(-3.0, 4.0);
}

Test(bounds, non_finite_half_bounded_domain) {
  set_domain(0.0, INFINITY);
  g_cfg_rw.init_max = 7.0;
  check_pinned(0.0, 7.0);
}
