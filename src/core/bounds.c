#include <math.h>

#include "algorithms/algorithm.h"

// Shared out-of-domain handling for algorithms that move points in continuous space
// GA never needs it: its decode always lands inside [domain_min, domain_max]

const EnumEntry bounds_table[] = {
    {"clamp",   BOUNDS_CLAMP  },
    {"reflect", BOUNDS_REFLECT},
    {"random",  BOUNDS_RANDOM },
    {NULL,      0             },
};

// Mirrors x back into [lo, hi] as if bouncing between the walls, however far out it is
// Returns 1 when the number of bounces is odd, i.e. when the direction of travel flips
static int reflect(double* x, double lo, double hi) {
  double width = hi - lo;
  double k     = floor((*x - lo) / width);  // signed number of walls crossed
  double t     = *x - lo - k * width;       // offset inside the final cell, in [0, width)
  int    odd   = fmod(fabs(k), 2.0) == 1.0;
  if (odd) t = width - t;
  *x = lo + t;
  // Rounding can leave the result a hair outside
  if (*x < lo) *x = lo;
  else if (*x > hi) *x = hi;
  return odd;
}

void apply_bounds(double* x, double* v, BoundsMode mode) {
  double lo = g_cfg.domain_min, hi = g_cfg.domain_max;

  // A coordinate that overflowed (or became NaN) has no meaningful reflection or bounce count,
  // and on an unbounded side no bound to return to: it's pinned to the nearest finite limit, the
  // init range's where the domain has none
  if (!isfinite(*x)) {
    if (isinf(lo)) lo = g_cfg.init_min;
    if (isinf(hi)) hi = g_cfg.init_max;
    *x = *x < lo ? lo : hi;
    if (v) *v = 0.0;
    return;
  }

  // From here on the violated bound is finite: a finite x can't be past an infinite one
  if (*x >= lo && *x <= hi) return;

  switch (mode) {
    case BOUNDS_CLAMP:
      *x = *x < lo ? lo : hi;
      if (v) *v = 0.0;
      return;
    case BOUNDS_REFLECT: {
      if (isfinite(lo) && isfinite(hi)) {
        if (reflect(x, lo, hi) && v) *v = -*v;
        return;
      }
      // A single wall: one bounce off it, landing at the wall if the mirror image overflows
      double wall   = *x < lo ? lo : hi;
      double mirror = 2.0 * wall - *x;
      *x            = isfinite(mirror) ? mirror : wall;
      if (v) *v = -*v;
      return;
    }
    case BOUNDS_RANDOM:  // both bounds are finite: check_bounds_mode rejects a single wall
      *x = rng_next_double_range(lo, hi);
      if (v) *v = 0.0;
      return;
  }
}

void check_bounds_mode(BoundsMode mode) {
  if (mode == BOUNDS_RANDOM && isfinite(g_cfg.domain_min) != isfinite(g_cfg.domain_max))
    die_with_hint("bounds = random needs a domain that is finite, or unbounded on both sides: "
                  "there is no uniform draw from a half-bounded one");
}
