#include <float.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "algorithm.h"

// --- SMA-private config state ---

typedef struct {
  int        pop_size;
  double     z;  // per-slime probability of full random re-init
  BoundsMode bounds;
} SmaConfig;

static _Thread_local SmaConfig cfg;

// --- Field table ---

static const FieldSpec sma_fields[] = {
    FIELD(SmaConfig, pop_size, INT, "30", "Population size"),
    FIELD(SmaConfig, z, DOUBLE, "0.03",
          "Full re-init probability (per slime), into the init range"),
    ENUM_FIELD(SmaConfig, bounds, bounds_table, "clamp",
               "Out-of-domain handling (ignored when the domain has no finite end)"),
    FIELDS_END,
};

static void* sma_cfg_base(void) { return &cfg; }

// --- Validate ---

static void sma_validate(void) {
  if (cfg.pop_size <= 0) die_with_hint("pop_size must be positive");
  if (cfg.z < 0.0 || cfg.z > 1.0) die_with_hint("z must be in [0.0, 1.0]");
  check_bounds_mode(cfg.bounds);
  // positions, weights and fits of every slime share one buffer
  check_fits_int(cfg.pop_size, 2LL * g_cfg.n_dims + 1, "pop_size x n_dims");
}

// --- Presentation ---

static void print_slime(const double* pos, int index, double fit, double DF) {
  if (fit == DF) printf(">> Slime %2d\n", index);
  else printf("   Slime %2d\n", index);
  printf("     pos: ");
  print_vec(pos);
  printf("\n");
  printf("     fitness = %g\n\n", fit);
}

static void print_pop(const double* positions, const double* fits, double DF) {
  for (int i = 0; i < cfg.pop_size; i++) print_slime(positions + i * g_cfg.n_dims, i, fits[i], DF);
  printf("DF = %g\n", DF);
}

// --- Entry point ---

static void sma_run(RunResult* out) {
  // Single slab for the three per-slime arrays, laid out end-to-end:
  // [ positions (pop_size*n) | weights (pop_size*n) | fits (pop_size) ]
  double* slab  = xmalloc((size_t) cfg.pop_size * (2 * g_cfg.n_dims + 1) * sizeof(double));
  double* Xb    = xmalloc((size_t) g_cfg.n_dims * sizeof(double));
  Ranked* order = xmalloc((size_t) cfg.pop_size * sizeof(Ranked));

  double* positions = slab;
  double* weights   = slab + cfg.pop_size * g_cfg.n_dims;
  double* fits      = slab + 2 * cfg.pop_size * g_cfg.n_dims;

  // Initial population: uniform random in the init range
  for (int i = 0; i < cfg.pop_size; i++) {
    for (int d = 0; d < g_cfg.n_dims; d++)
      positions[i * g_cfg.n_dims + d] = rng_next_double_range(g_cfg.init_min, g_cfg.init_max);
    fits[i] = evaluate(positions + i * g_cfg.n_dims);
  }

  // DF / Xb track best-ever, seeded from the first slime so they always hold a real one
  double DF = fits[0];
  memcpy(Xb, positions, g_cfg.n_dims * sizeof(double));
  for (int i = 1; i < cfg.pop_size; i++) {
    if (is_better(fits[i], DF)) {
      DF = fits[i];
      memcpy(Xb, positions + i * g_cfg.n_dims, g_cfg.n_dims * sizeof(double));
    }
  }

  if (g_opts.interactive) {
    printf("Initial Population\n\n");
    print_pop(positions, fits, DF);
    wait_for_enter();
    print_separator();
  }

  StopState stop = stop_init();
  for (int it = 0; it < g_cfg.iterations; it++) {
    // --- Rank by fitness ---
    // SMA needs the best and worst fitness this iteration, plus a top/bottom split for the weight
    // formula
    rank_by_fitness(fits, cfg.pop_size, order, RANK_BEST_FIRST);

    double bF = order[0].fitness;
    double wF = order[cfg.pop_size - 1].fitness;
    // Direction-aware eps to avoid div/0 when all fitnesses are equal, without
    // flipping the denominator's sign (negative for minimize, positive for maximize).
    double S = (bF - wF) + (g_cfg.direction == MINIMIZE ? -DBL_EPSILON : DBL_EPSILON);

    // --- Weight update (per-slime per-dim
    // Top half (ranks 0..ceil(pop_size/2) - 1) get 1 + r*log; bottom half gets 1 - r*log.
    // Rounding up matches the reference implementation: the middle slime of an odd
    // population is attracted, not repelled
    int half = (cfg.pop_size + 1) / 2;
    for (int rank = 0; rank < cfg.pop_size; rank++) {
      int    i      = order[rank].index;
      double logval = log10((bF - fits[i]) / S + 1.0);
      for (int d = 0; d < g_cfg.n_dims; d++) {
        double r                      = rng_next_double();
        weights[i * g_cfg.n_dims + d] = (rank < half) ? 1.0 + r * logval : 1.0 - r * logval;
      }
    }

    // --- Decay coefficients ---
    // Use (it+1)/T, 1-indexed
    double t_frac = (double) (it + 1) / (double) g_cfg.iterations;
    double a      = atanh(-t_frac + 1.0);  // vb range: [-a, a], shrinks to 0
    double b      = 1.0 - t_frac;          // vc range: [-b, b], shrinks to 0

    // --- Position update ---
    for (int i = 0; i < cfg.pop_size; i++) {
      if (rng_next_double() < cfg.z) {
        // Whole-slime re-init - one draw per slime
        // A re-initialization, so it draws from the init range, like the initial population
        for (int d = 0; d < g_cfg.n_dims; d++)
          positions[i * g_cfg.n_dims + d] = rng_next_double_range(g_cfg.init_min, g_cfg.init_max);
      } else {
        double p = tanh(fabs(fits[i] - DF));
        for (int d = 0; d < g_cfg.n_dims; d++) {
          if (rng_next_double() < p) {
            // Exploit around Xb using two independent peer draws
            int    A  = rng_next_int(0, cfg.pop_size);
            int    B  = rng_next_int(0, cfg.pop_size);
            double vb = rng_next_double_range(-a, a);
            positions[i * g_cfg.n_dims + d] =
                Xb[d] + vb * (weights[i * g_cfg.n_dims + d] * positions[A * g_cfg.n_dims + d] -
                              positions[B * g_cfg.n_dims + d]);
          } else {
            double vc                       = rng_next_double_range(-b, b);
            positions[i * g_cfg.n_dims + d] = vc * positions[i * g_cfg.n_dims + d];
          }
        }
      }

      // Bring back into the domain (the re-init branch is already inside, so it's a no-op there)
      for (int d = 0; d < g_cfg.n_dims; d++)
        apply_bounds(&positions[i * g_cfg.n_dims + d], NULL, cfg.bounds);

      fits[i] = evaluate(positions + i * g_cfg.n_dims);

      // Opportunistic DF update within the iteration: later slimes see the fresh Xb
      // Matches PSO's async gbest
      if (is_better(fits[i], DF)) {
        DF = fits[i];
        memcpy(Xb, positions + i * g_cfg.n_dims, g_cfg.n_dims * sizeof(double));
      }
    }

    if (g_opts.interactive) {
      print_iteration_header(it);
      print_pop(positions, fits, DF);
      wait_for_enter();
      print_separator();
    }
    if (end_iteration(&stop, DF)) break;
  }

  memcpy(out->pos, Xb, g_cfg.n_dims * sizeof(double));
  out->fitness = DF;
  out->stop    = stop;

  free(order);
  free(Xb);
  free(slab);
}

const Algorithm sma_algorithm = {
    .name     = "sma",
    .fields   = sma_fields,
    .cfg_base = sma_cfg_base,
    .cfg_size = sizeof(SmaConfig),
    .validate = sma_validate,
    .run      = sma_run,
};
