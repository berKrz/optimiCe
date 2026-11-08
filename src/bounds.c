#include <math.h>

#include "algorithms.h"
#include "common.h"

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
  if (*x >= lo && *x <= hi) return;

  // A non-finite coordinate has no meaningful reflection or bounce count; pin it
  if (!isfinite(*x)) mode = BOUNDS_CLAMP;

  switch (mode) {
    case BOUNDS_CLAMP:
      *x = *x < lo ? lo : hi;
      if (v) *v = 0.0;
      return;
    case BOUNDS_REFLECT:
      if (reflect(x, lo, hi) && v) *v = -*v;
      return;
    case BOUNDS_RANDOM:
      *x = rng_next_double_range(lo, hi);
      if (v) *v = 0.0;
      return;
  }
}
