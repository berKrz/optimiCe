#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include "common.h"

// Apply_seed calls rng_init_with during INI parsing;
// Parse_args calls rng_init_auto afterwards as the fallback.
static void rng_commit(uint32_t seed) {
  static int called = 0;
  if (called) return;
  called     = 1;
  g_cfg.seed = seed;
  srand((unsigned int) seed);
}

void rng_init_with(uint32_t seed) { rng_commit(seed); }

void rng_init_auto(void) { rng_commit((uint32_t) time(NULL) ^ (uint32_t) getpid()); }

double rng_next_double(void) { return (double) rand() / ((double) RAND_MAX + 1.0); }

double rng_next_double_range(double lo, double hi) { return lo + (hi - lo) * rng_next_double(); }

int rng_next_int(int lo, int hi) { return lo + (int) (rng_next_double() * (hi - lo)); }

int rng_next_bit(void) { return rng_next_int(0, 2); }
