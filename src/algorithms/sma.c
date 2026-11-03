#include <errno.h>
#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "algorithms.h"
#include "common.h"

// --- SMA-private config state ---

typedef struct {
  int    pop_size;
  int    iterations;
  double z;  // per-slime probability of full random re-init
} SmaConfig;

static _Thread_local SmaConfig cfg;

// --- Field handlers ---

static void apply_pop_size(const char* source, const char* key, const char* val) {
  char* end;
  errno  = 0;
  long v = strtol(val, &end, 10);
  if (*val == '\0' || *end != '\0' || errno == ERANGE || v <= 0 || v > INT_MAX)
    die_at(source, key, "must be a positive integer");
  cfg.pop_size = (int) v;
}

static void apply_iterations(const char* source, const char* key, const char* val) {
  char* end;
  errno  = 0;
  long v = strtol(val, &end, 10);
  if (*val == '\0' || *end != '\0' || errno == ERANGE || v <= 0 || v > INT_MAX)
    die_at(source, key, "must be a positive integer");
  cfg.iterations = (int) v;
}

static void apply_z(const char* source, const char* key, const char* val) {
  char* end;
  errno    = 0;
  double v = strtod(val, &end);
  if (*val == '\0' || *end != '\0' || errno == ERANGE || v < 0.0 || v > 1.0)
    die_at(source, key, "must be a number in [0.0, 1.0]");
  cfg.z = v;
}

// --- Field table ---

const FieldSpec sma_fields[] = {
    {"pop_size",   FIELD_INT,    "30",   "Population size",                      apply_pop_size  },
    {"iterations", FIELD_INT,    "100",  "Number of iterations",                 apply_iterations},
    {"z",          FIELD_DOUBLE, "0.03", "Full re-init probability (per slime)", apply_z         },
    {NULL,         0,            NULL,   NULL,                                   NULL            },
};

// --- Dump ---

void sma_dump(void) {
  printf("pop_size   = %d\n", cfg.pop_size);
  printf("iterations = %d\n", cfg.iterations);
  printf("z          = %g\n", cfg.z);
}

// --- Fitness ranking ---
//
// SMA needs the best and worst fitness this iteration, plus a top/bottom split for the weight
// formula
// We rank by sorting (fitness, index) pairs, keeping positions in place and reading them
// through the index after the sort

typedef struct {
  double fit;
  int    idx;
} FitIdx;

static int cmp_min(const void* a, const void* b) {
  double fa = ((const FitIdx*) a)->fit;
  double fb = ((const FitIdx*) b)->fit;
  return (fa > fb) - (fa < fb);
}

static int cmp_max(const void* a, const void* b) {
  double fa = ((const FitIdx*) a)->fit;
  double fb = ((const FitIdx*) b)->fit;
  return (fa < fb) - (fa > fb);
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
  int n = g_cfg.n_dims;
  for (int i = 0; i < cfg.pop_size; i++) print_slime(positions + i * n, i, fits[i], DF);
  printf("DF = %g\n", DF);
}

// --- Entry point ---

void sma_run(void) {
  int n        = g_cfg.n_dims;
  int pop_size = cfg.pop_size;
  int T        = cfg.iterations;

  // Single slab for the three per-slime arrays, laid out end-to-end:
  // [ positions (pop_size*n) | weights (pop_size*n) | fits (pop_size) ]
  double* slab  = malloc(pop_size * (2 * n + 1) * sizeof(double));
  double* Xb    = malloc(n * sizeof(double));
  FitIdx* order = malloc(pop_size * sizeof(FitIdx));

  if (!slab || !Xb || !order) die("memory allocation failed");

  double* positions = slab;
  double* weights   = slab + pop_size * n;
  double* fits      = slab + 2 * pop_size * n;

  // Initial population: uniform random in [domain_min, domain_max]
  for (int i = 0; i < pop_size; i++) {
    for (int d = 0; d < n; d++)
      positions[i * n + d] = rng_next_double_range(g_cfg.domain_min, g_cfg.domain_max);
    fits[i] = g_cfg.fitness_fn(positions + i * n);
  }

  // DF / Xb track best-ever
  double DF = g_cfg.direction == MINIMIZE ? DBL_MAX : -DBL_MAX;
  for (int i = 0; i < pop_size; i++) {
    if (is_better(fits[i], DF)) {
      DF = fits[i];
      memcpy(Xb, positions + i * n, n * sizeof(double));
    }
  }

  if (g_cfg.interactive) {
    printf("Initial Population\n\n");
    print_pop(positions, fits, DF);
    wait_for_enter();
    print_separator();
  }

  int (*cmp)(const void*, const void*) = g_cfg.direction == MINIMIZE ? cmp_min : cmp_max;

  for (int it = 0; it < T; it++) {
    // --- Rank by fitness ---
    for (int i = 0; i < pop_size; i++) {
      order[i].fit = fits[i];
      order[i].idx = i;
    }
    qsort(order, pop_size, sizeof(FitIdx), cmp);

    double bF = order[0].fit;
    double wF = order[pop_size - 1].fit;
    // Direction-aware eps to avoid div/0 when all fitnesses are equal, without
    // flipping the denominator's sign (negative for minimize, positive for maximize).
    double S = (bF - wF) + (g_cfg.direction == MINIMIZE ? -DBL_EPSILON : DBL_EPSILON);

    if (is_better(bF, DF)) {
      DF = bF;
      memcpy(Xb, positions + order[0].idx * n, n * sizeof(double));
    }

    // --- Weight update (per-slime per-dim
    // Top half (ranks 0..pop_size/2 - 1) get 1 + r*log; bottom half gets 1 - r*log
    int half = pop_size / 2;
    for (int rank = 0; rank < pop_size; rank++) {
      int    i      = order[rank].idx;
      double logval = log10((bF - fits[i]) / S + 1.0);
      for (int d = 0; d < n; d++) {
        double r           = rng_next_double();
        weights[i * n + d] = (rank < half) ? 1.0 + r * logval : 1.0 - r * logval;
      }
    }

    // --- Decay coefficients ---
    // Use (it+1)/T, 1-indexed
    double t_frac = (double) (it + 1) / (double) T;
    double a      = atanh(-t_frac + 1.0);  // vb range: [-a, a], shrinks to 0
    double b      = 1.0 - t_frac;          // vc range: [-b, b], shrinks to 0

    // --- Position update ---
    for (int i = 0; i < pop_size; i++) {
      if (rng_next_double() < cfg.z) {
        // Whole-slime re-init - one draw per slime
        for (int d = 0; d < n; d++)
          positions[i * n + d] = rng_next_double_range(g_cfg.domain_min, g_cfg.domain_max);
      } else {
        double p = tanh(fabs(fits[i] - DF));
        for (int d = 0; d < n; d++) {
          if (rng_next_double() < p) {
            // Exploit around Xb using two independent peer draws
            int    A  = rng_next_int(0, pop_size);
            int    B  = rng_next_int(0, pop_size);
            double vb = rng_next_double_range(-a, a);
            positions[i * n + d] =
                Xb[d] + vb * (weights[i * n + d] * positions[A * n + d] - positions[B * n + d]);
          } else {
            double vc            = rng_next_double_range(-b, b);
            positions[i * n + d] = vc * positions[i * n + d];
          }
        }
      }

      // Bring back to boundaries (the re-init branch is already in bounds; cheap to clamp
      // uniformly)
      for (int d = 0; d < n; d++) {
        if (positions[i * n + d] < g_cfg.domain_min) positions[i * n + d] = g_cfg.domain_min;
        else if (positions[i * n + d] > g_cfg.domain_max) positions[i * n + d] = g_cfg.domain_max;
      }

      fits[i] = g_cfg.fitness_fn(positions + i * n);

      // Opportunistic DF update within the iteration: later slimes see the fresh Xb
      // Matches PSO's async gbest
      if (is_better(fits[i], DF)) {
        DF = fits[i];
        memcpy(Xb, positions + i * n, n * sizeof(double));
      }
    }

    if (g_cfg.interactive) {
      print_iteration_header(it);
      print_pop(positions, fits, DF);
      wait_for_enter();
      print_separator();
    } else {
      print_iteration_line(it, DF);
    }
  }

  print_result(Xb, DF);

  free(order);
  free(Xb);
  free(slab);
}
