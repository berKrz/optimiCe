#include <float.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "algorithm.h"

// --- Config ---

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

// --- State ---

// One buffer holds the three per-slime arrays end to end:
//   [ positions (pop_size * n_dims) | weights (pop_size * n_dims) | fits (pop_size) ]
// Slime i's position is the n_dims doubles at positions + i * n_dims; its weights sit likewise
typedef struct {
  double* data;
  double* positions;
  double* weights;
  double* fits;
  Ranked* order;         // pop_size: the slimes ranked best first, refreshed every iteration
  double* best_pos;      // n_dims: where the best fitness so far was found
  double  best_fitness;  // best fitness so far, over the whole run
} Population;

static Population population_alloc(void) {
  size_t     n = (size_t) cfg.pop_size, dims = (size_t) g_cfg.n_dims;
  Population pop = {
      .data     = xmalloc(n * (2 * dims + 1) * sizeof(double)),
      .order    = xmalloc(n * sizeof(Ranked)),
      .best_pos = xmalloc(dims * sizeof(double)),
  };
  pop.positions = pop.data;
  pop.weights   = pop.data + n * dims;
  pop.fits      = pop.data + 2 * n * dims;
  return pop;
}

static void population_free(Population* pop) {
  free(pop->best_pos);
  free(pop->order);
  free(pop->data);
}

static double* position_of(const Population* pop, int i) {
  return pop->positions + i * g_cfg.n_dims;
}

static double* weights_of(const Population* pop, int i) { return pop->weights + i * g_cfg.n_dims; }

// --- Steps ---

static void copy_pos(double* dst, const double* src) {
  memcpy(dst, src, (size_t) g_cfg.n_dims * sizeof(double));
}

static void update_best(Population* pop, int i) {
  if (is_better(pop->fits[i], pop->best_fitness)) {
    pop->best_fitness = pop->fits[i];
    copy_pos(pop->best_pos, position_of(pop, i));
  }
}

// Initial population: uniform random in the init range
static void init_population(Population* pop) {
  for (int i = 0; i < cfg.pop_size; i++) {
    double* x = position_of(pop, i);
    for (int d = 0; d < g_cfg.n_dims; d++)
      x[d] = rng_next_double_range(g_cfg.init_min, g_cfg.init_max);
    pop->fits[i] = evaluate(x);
  }

  // The first slime seeds the best, so it always holds a real slime, never a placeholder
  pop->best_fitness = pop->fits[0];
  copy_pos(pop->best_pos, position_of(pop, 0));
  for (int i = 1; i < cfg.pop_size; i++) update_best(pop, i);
}

// Ranks the slimes by fitness and gives each one its weights for this iteration, one per
// dimension
// The better half (ranks 0 .. ceil(pop_size / 2) - 1) gets 1 + r * log_term, the rest
// 1 - r * log_term. Rounding up matches the reference implementation: the middle slime of an odd
// population is attracted, not repelled
static void update_weights(Population* pop) {
  rank_by_fitness(pop->fits, cfg.pop_size, pop->order, RANK_BEST_FIRST);

  double iter_best  = pop->order[0].fitness;
  double iter_worst = pop->order[cfg.pop_size - 1].fitness;
  // Direction-aware eps to avoid div/0 when all fitnesses are equal, without flipping the
  // denominator's sign (negative for minimize, positive for maximize)
  double eps    = g_cfg.direction == MINIMIZE ? -DBL_EPSILON : DBL_EPSILON;
  double spread = (iter_best - iter_worst) + eps;

  int half = (cfg.pop_size + 1) / 2;
  for (int rank = 0; rank < cfg.pop_size; rank++) {
    int     i        = pop->order[rank].index;
    double* w        = weights_of(pop, i);
    double  log_term = log10((iter_best - pop->fits[i]) / spread + 1.0);
    for (int d = 0; d < g_cfg.n_dims; d++) {
      double r = rng_next_double();
      w[d]     = (rank < half) ? 1.0 + r * log_term : 1.0 - r * log_term;
    }
  }
}

// Moves every slime for iteration `iter` (0-based), then evaluates it
// In the paper's symbols: best_pos is Xb and best_fitness DF; a slime approaches the best with
// probability p (approach_prob), as Xb + vb * (W * X_A - X_B), where vb (pull) is drawn from
// [-a, a] (pull_range) and A, B are two random slimes (peer_a, peer_b); otherwise it contracts,
// X = vc * X, with vc (decay) drawn from [-b, b] (decay_range)
static void move_slimes(Population* pop, int iter) {
  // Both ranges shrink to 0 over the run; the progress is 1-indexed, (iter + 1) / iterations
  double progress    = (double) (iter + 1) / (double) g_cfg.iterations;
  double pull_range  = atanh(-progress + 1.0);
  double decay_range = 1.0 - progress;

  for (int i = 0; i < cfg.pop_size; i++) {
    double* x = position_of(pop, i);
    double* w = weights_of(pop, i);

    if (rng_next_double() < cfg.z) {
      // Whole-slime re-init - one draw per slime
      // A re-initialization, so it draws from the init range, like the initial population
      for (int d = 0; d < g_cfg.n_dims; d++)
        x[d] = rng_next_double_range(g_cfg.init_min, g_cfg.init_max);
    } else {
      double approach_prob = tanh(fabs(pop->fits[i] - pop->best_fitness));
      for (int d = 0; d < g_cfg.n_dims; d++) {
        if (rng_next_double() < approach_prob) {
          // Two independent peer draws; a peer already moved this iteration is read as moved
          const double* peer_a = position_of(pop, rng_next_int(0, cfg.pop_size));
          const double* peer_b = position_of(pop, rng_next_int(0, cfg.pop_size));
          double        pull   = rng_next_double_range(-pull_range, pull_range);
          x[d]                 = pop->best_pos[d] + pull * (w[d] * peer_a[d] - peer_b[d]);
        } else {
          double decay = rng_next_double_range(-decay_range, decay_range);
          x[d]         = decay * x[d];
        }
      }
    }

    // Bring back into the domain (the re-init branch is already inside, so it's a no-op there)
    for (int d = 0; d < g_cfg.n_dims; d++) apply_bounds(&x[d], NULL, cfg.bounds);

    pop->fits[i] = evaluate(x);

    // The best is updated within the iteration, so later slimes already see it: PSO's async gbest
    update_best(pop, i);
  }
}

// --- Interactive output ---

static void print_slime(const double* pos, int index, double fit, double best_fitness) {
  if (fit == best_fitness) printf(">> Slime %2d\n", index);
  else printf("   Slime %2d\n", index);
  printf("     pos: ");
  print_vec(pos);
  printf("\n");
  printf("     fitness = %g\n\n", fit);
}

// The whole population, then a pause
static void show_population(const Population* pop) {
  for (int i = 0; i < cfg.pop_size; i++)
    print_slime(position_of(pop, i), i, pop->fits[i], pop->best_fitness);
  printf("best = %g\n", pop->best_fitness);
  wait_for_enter();
  print_separator();
}

// --- Entry point ---

static void sma_run(RunResult* out) {
  Population pop = population_alloc();
  init_population(&pop);

  if (g_opts.interactive) {
    printf("Initial Population\n\n");
    show_population(&pop);
  }

  StopState stop = stop_init();
  for (int i = 0; i < g_cfg.iterations; i++) {
    update_weights(&pop);
    move_slimes(&pop, i);

    if (g_opts.interactive) {
      print_iteration_header(i);
      show_population(&pop);
    }
    if (end_iteration(&stop, pop.best_fitness)) break;
  }

  copy_pos(out->pos, pop.best_pos);
  out->fitness = pop.best_fitness;
  out->stop    = stop;

  population_free(&pop);
}

const Algorithm sma_algorithm = {
    .name     = "sma",
    .fields   = sma_fields,
    .cfg_base = sma_cfg_base,
    .cfg_size = sizeof(SmaConfig),
    .validate = sma_validate,
    .run      = sma_run,
};
