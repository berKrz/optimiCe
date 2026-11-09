#include <stdint.h>
#include <time.h>
#include <unistd.h>

#include "common.h"

// xoshiro256** (Blackman & Vigna), state seeded through splitmix64
// The state is _Thread_local: every thread draws from its own stream, so a run's numbers depend
// only on its seed, never on what other threads are doing
// All-zero state is xoshiro's one fixed point; splitmix64 never produces four zero outputs in a
// row, so rng_init can't land on it. Drawing before any rng_init on a thread would return zeros
static _Thread_local uint64_t state[4];

static uint64_t splitmix64(uint64_t* x) {
  uint64_t z = (*x += 0x9e3779b97f4a7c15ULL);
  z          = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
  z          = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
  return z ^ (z >> 31);
}

static inline uint64_t rotl(uint64_t x, int k) { return (x << k) | (x >> (64 - k)); }

static uint64_t next(void) {
  uint64_t result = rotl(state[1] * 5, 7) * 9;
  uint64_t t      = state[1] << 17;

  state[2] ^= state[0];
  state[3] ^= state[1];
  state[1] ^= state[2];
  state[0] ^= state[3];
  state[2] ^= t;
  state[3]  = rotl(state[3], 45);

  return result;
}

uint32_t rng_generate_seed(void) { return (uint32_t) time(NULL) ^ (uint32_t) getpid(); }

void rng_init(uint32_t seed) {
  uint64_t x = seed;
  for (int i = 0; i < 4; i++) state[i] = splitmix64(&x);
}

// Top 53 bits as a double in [0, 1)
double rng_next_double(void) { return (double) (next() >> 11) * 0x1.0p-53; }

double rng_next_double_range(double lo, double hi) { return lo + (hi - lo) * rng_next_double(); }

int rng_next_int(int lo, int hi) { return lo + (int) (rng_next_double() * (hi - lo)); }

int rng_next_bit(void) { return rng_next_int(0, 2); }
