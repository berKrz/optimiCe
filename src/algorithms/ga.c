#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "algorithms.h"
#include "common.h"

// --- GA-private config state ---

typedef enum { ROULETTE, ROULETTE_NORMALIZED, RANK, TOURNAMENT } SelectionKind;
typedef enum { XOVER_N_POINT, XOVER_UNIFORM } CrossoverKind;
typedef enum { ELITISM_NONE, ELITISM_FAMILY } ElitismKind;

typedef struct {
  int           pop_size;
  int           ind_size;  // bits per dimension (capped at 63 so 1ULL << ind_size is defined)
  int           generations;
  int           tournament_size;
  double        rank_pressure;  // linear ranking: expected copies of the best, in [1.0, 2.0]
  DoubleList    cut_points;  // n_point cut ratios in (0.0, 1.0), increasing, applied per-dimension
  double        mutation_rate;  // per-bit flip probability in [0.0, 1.0)
  SelectionKind selection;
  CrossoverKind crossover;
  ElitismKind   elitism;
} GaConfig;

static _Thread_local GaConfig cfg;

// --- Bit-packed individual layout ---
//
// The population is one flat uint64_t[pop_size * words_per_ind]. Each
// individual gets n_dims * ind_size contiguous bits (little-endian within
// its own slice: bit b of dimension d lives at global index d * ind_size + b).
// Decoded dimensions come out as unsigned integers in [0, 2^ind_size - 1],
// then linearly mapped onto [domain_min, domain_max]

static int words_per_ind(void) {
  int total_bits = g_cfg.n_dims * cfg.ind_size;
  return (total_bits + 63) / 64;
}

static inline int bit_get(const uint64_t* ind, int i) {
  return (int) ((ind[i / 64] >> (i % 64)) & 1ULL);
}

static inline void bit_set(uint64_t* ind, int i, int v) {
  uint64_t mask = 1ULL << (i % 64);
  if (v) ind[i / 64] |= mask;
  else ind[i / 64] &= ~mask;
}

static inline void bit_flip(uint64_t* ind, int i) { ind[i / 64] ^= (1ULL << (i % 64)); }

// --- Selection / crossover lookup tables ---

static const EnumEntry selection_table[] = {
    {"roulette",            ROULETTE           },
    {"roulette_normalized", ROULETTE_NORMALIZED},
    {"rank",                RANK               },
    {"tournament",          TOURNAMENT         },
    {NULL,                  0                  },
};

static const EnumEntry elitism_table[] = {
    {"none",   ELITISM_NONE  },
    {"family", ELITISM_FAMILY},
    {NULL,     0             },
};

static const EnumEntry crossover_table[] = {
    {"n_point", XOVER_N_POINT},
    {"uniform", XOVER_UNIFORM},
    {NULL,      0            },
};

// --- Field table ---

const FieldSpec ga_fields[] = {
    {"pop_size", FIELD_INT, offsetof(GaConfig, pop_size), NULL, "15", "Population size (>= 2)"},
    {"ind_size", FIELD_INT, offsetof(GaConfig, ind_size), NULL, "22", "Bits per dimension [2, 63]"},
    {"generations", FIELD_INT, offsetof(GaConfig, generations), NULL, "30",
     "Number of generations"},
    {"tournament_size", FIELD_INT, offsetof(GaConfig, tournament_size), NULL, "2",
     "Individuals per tournament"},
    {"rank_pressure", FIELD_DOUBLE, offsetof(GaConfig, rank_pressure), NULL, "1.5",
     "Rank selection pressure in [1.0, 2.0]"},
    {"cut_points", FIELD_DOUBLE_LIST, offsetof(GaConfig, cut_points), NULL, "[0.6]",
     "n_point cut ratios in (0.0, 1.0), increasing"},
    {"mutation_rate", FIELD_DOUBLE, offsetof(GaConfig, mutation_rate), NULL, "0.01",
     "Per-bit flip rate in [0.0, 1.0)"},
    {"selection", FIELD_ENUM, offsetof(GaConfig, selection), selection_table, "roulette",
     "Selection: roulette | roulette_normalized | rank | tournament"},
    {"crossover", FIELD_ENUM, offsetof(GaConfig, crossover), crossover_table, "n_point",
     "Crossover: n_point | uniform"},
    {"elitism", FIELD_ENUM, offsetof(GaConfig, elitism), elitism_table, "none",
     "Elitism: none | family"},
    {NULL, 0, 0, NULL, NULL, NULL},
};

void* ga_cfg_base(void) { return &cfg; }

// --- Validate ---

// Cut index of ratio r within a dimension's slice
static int cut_index(double r) { return (int) (cfg.ind_size * r); }

// Each ratio must lie in (0, 1) and be strictly increasing, and the resulting cut indices must be
// distinct and >= 1, so every cut actually splits the slice
// A cut at index 0 only flips which segments are kept vs swapped, yielding the same pair of
// children, and two ratios landing on the same bit collapse into one cut
// Either way the user would silently get fewer cuts than asked for
static void validate_cut_points(void) {
  const DoubleList* c = &cfg.cut_points;
  char              msg[192];
  if (c->n < 1) die("ga: cut_points must have at least one element");
  for (int i = 0; i < c->n; i++) {
    if (!(c->v[i] > 0.0 && c->v[i] < 1.0)) {
      snprintf(msg, sizeof(msg), "ga: cut_points[%d] = %g must be in (0.0, 1.0)", i, c->v[i]);
      die(msg);
    }
    if (i > 0 && !(c->v[i] > c->v[i - 1])) {
      snprintf(msg, sizeof(msg), "ga: cut_points must be strictly increasing (%g after %g)",
               c->v[i], c->v[i - 1]);
      die(msg);
    }
    int idx = cut_index(c->v[i]);
    if (idx < 1) {
      snprintf(msg, sizeof(msg),
               "ga: cut_points[%d] = %g lands on bit 0 with ind_size = %d (needs >= %g)", i,
               c->v[i], cfg.ind_size, 1.0 / cfg.ind_size);
      die(msg);
    }
    if (i > 0 && idx == cut_index(c->v[i - 1])) {
      snprintf(msg, sizeof(msg),
               "ga: cut_points %g and %g land on the same bit (%d) with ind_size = %d", c->v[i - 1],
               c->v[i], idx, cfg.ind_size);
      die(msg);
    }
  }
}

void ga_validate(void) {
  if (cfg.pop_size < 2) die("ga: pop_size must be >= 2");
  if (cfg.ind_size < 2 || cfg.ind_size > 63) die("ga: ind_size must be in [2, 63]");
  if (cfg.generations < 1) die("ga: generations must be positive");
  if (!(cfg.mutation_rate >= 0.0 && cfg.mutation_rate < 1.0))
    die("ga: mutation_rate must be in [0.0, 1.0)");
  // tournament_size is ignored under other selections, so it's only checked when in use
  if (cfg.selection == TOURNAMENT) {
    if (cfg.tournament_size < 2) die("ga: tournament_size must be >= 2");
    if (cfg.tournament_size > cfg.pop_size) {
      char msg[160];
      snprintf(msg, sizeof(msg),
               "ga: tournament_size must be at most pop_size (pop_size=%d, tournament_size=%d)",
               cfg.pop_size, cfg.tournament_size);
      die(msg);
    }
  }
  // cut_points is only checked when in use
  if (cfg.crossover == XOVER_N_POINT) validate_cut_points();
  // rank_pressure is only checked when in use
  if (cfg.selection == RANK && !(cfg.rank_pressure >= 1.0 && cfg.rank_pressure <= 2.0))
    die("ga: rank_pressure must be in [1.0, 2.0]");
}

// --- Dump ---

void ga_dump(void) {
  printf("pop_size        = %d\n", cfg.pop_size);
  printf("ind_size        = %d\n", cfg.ind_size);
  printf("generations     = %d\n", cfg.generations);
  printf("tournament_size = %d\n", cfg.tournament_size);
  printf("rank_pressure   = %g\n", cfg.rank_pressure);
  printf("cut_points      = [");
  for (int i = 0; i < cfg.cut_points.n; i++) printf(i ? ", %g" : "%g", cfg.cut_points.v[i]);
  printf("]\n");
  printf("mutation_rate   = %g\n", cfg.mutation_rate);
  printf("selection       = %s\n", enum_name(selection_table, cfg.selection));
  printf("crossover       = %s\n", enum_name(crossover_table, cfg.crossover));
  printf("elitism         = %s\n", enum_name(elitism_table, cfg.elitism));
}

// --- Decode / eval ---

// Reads ind_size bits of dimension d as an unsigned int and linearly maps it onto [domain_min,
// domain_max]. ind_size <= 63 guarantees 1ULL << ind_size stays well-defined
static double decode_dim(const uint64_t* ind, int d) {
  int      base = d * cfg.ind_size;
  uint64_t n    = 0;
  for (int b = 0; b < cfg.ind_size; b++)
    if (bit_get(ind, base + b)) n |= (1ULL << b);
  double max_n = (double) ((1ULL << cfg.ind_size) - 1ULL);
  return g_cfg.domain_min + (double) n * (g_cfg.domain_max - g_cfg.domain_min) / max_n;
}

static void decode_linear(const uint64_t* ind, double* pos) {
  for (int d = 0; d < g_cfg.n_dims; d++) pos[d] = decode_dim(ind, d);
}

// Decodes `ind` into `tmp` and returns its fitness
// `tmp` must be at least n_dims doubles; its contents are overwritten
static double eval(const uint64_t* ind, double* tmp) {
  decode_linear(ind, tmp);
  return g_cfg.fitness_fn(tmp);
}

// Scans the population and returns the index of its current best individual
// Caller provides `tmp` (n_dims doubles) as the decode scratch buffer
static int find_best(const uint64_t* pop, double* tmp) {
  int    words    = words_per_ind();
  int    best_idx = 0;
  double best_fit = eval(pop, tmp);
  for (int i = 1; i < cfg.pop_size; i++) {
    double f = eval(pop + i * words, tmp);
    if (is_better(f, best_fit)) {
      best_fit = f;
      best_idx = i;
    }
  }
  return best_idx;
}

// --- Operators ---

// Exchanges bit gi between a and b
static inline void swap_bit(uint64_t* a, uint64_t* b, int gi) {
  if (bit_get(a, gi) != bit_get(b, gi)) {
    bit_flip(a, gi);
    bit_flip(b, gi);
  }
}

// Dimension-aware n-point crossover: within each dimension's slice, the cuts split the bits into
// segments that alternate keep / swap, starting with keep
// With one cut this is single-point bits [cut, ind_size) are swapped
static void crossover_n_point(uint64_t* a, uint64_t* b) {
  const DoubleList* c = &cfg.cut_points;
  for (int d = 0; d < g_cfg.n_dims; d++) {
    int base = d * cfg.ind_size;
    int swap = 0, next = 0;  // next: index of the next cut to cross
    for (int i = 0; i < cfg.ind_size; i++) {
      while (next < c->n && i == cut_index(c->v[next])) {
        swap = !swap;
        next++;
      }
      if (swap) swap_bit(a, b, base + i);
    }
  }
}

// Uniform crossover: every bit is exchanged independently with probability 0.5
static void crossover_uniform(uint64_t* a, uint64_t* b) {
  int total_bits = g_cfg.n_dims * cfg.ind_size;
  for (int i = 0; i < total_bits; i++)
    if (rng_next_bit()) swap_bit(a, b, i);
}

// Per-bit flip at `mutation_rate`
// In interactive mode, prints a per-individual header on the first flip and each flipped bit index
// underneath
static void mutation(uint64_t* ind, int index) {
  int total_bits = g_cfg.n_dims * cfg.ind_size;
  int header     = 0;
  for (int i = 0; i < total_bits; i++) {
    if (rng_next_double() < cfg.mutation_rate) {
      if (g_cfg.interactive) {
        if (!header) {
          header = 1;
          printf("  Mutation: individual %d\n", index);
        }
        printf("    bit %d flipped\n", i);
      }
      bit_flip(ind, i);
    }
  }
}

// --- Per-run workspace ---

// Scratch buffers whose size is fixed for the whole run, allocated once in ga_run instead of per
// generation
// Selection reads the population into `aux` and `fits`, wheel selections fill `weights` (and
// `order` for rank), and `tmp` is the decode scratch for anything that evaluates an individual
typedef struct {
  double fit;
  int    idx;
} FitIdx;

typedef struct {
  double*   fits;     // pop_size
  double*   weights;  // pop_size
  FitIdx*   order;    // pop_size, rank's sort buffer
  int*      picked;   // pop_size, picked[i] = which `aux` / `fits` entry selection put in slot i
  uint64_t* aux;      // pop_size * words, pre-selection copy of the population
  uint64_t* family;   // 4 * words, parents + children of the pair under family elitism
  double*   tmp;      // n_dims
} GaWork;

static GaWork work_alloc(void) {
  size_t n = (size_t) cfg.pop_size;
  GaWork w = {
      .fits    = malloc(n * sizeof(double)),
      .weights = malloc(n * sizeof(double)),
      .order   = malloc(n * sizeof(FitIdx)),
      .picked  = malloc(n * sizeof(int)),
      .aux     = malloc(n * (size_t) words_per_ind() * sizeof(uint64_t)),
      .family  = malloc(4 * (size_t) words_per_ind() * sizeof(uint64_t)),
      .tmp     = malloc((size_t) g_cfg.n_dims * sizeof(double)),
  };
  if (!w.fits || !w.weights || !w.order || !w.picked || !w.aux || !w.family || !w.tmp)
    die("memory allocation failed");
  return w;
}

static void work_free(GaWork* w) {
  free(w->fits);
  free(w->weights);
  free(w->order);
  free(w->picked);
  free(w->aux);
  free(w->family);
  free(w->tmp);
}

// --- Selection ---

// Wheel weights: each turns raw fitness into non-negative weights that grow
// All of them fill `weights` (pop_size entries) from `fits` and are direction-aware

// roulette: shift by the population's extreme so the worst individual gets weight 1
// Minimization uses (max - f) + 1, maximization (f - min) + 1
// The +1 is in fitness units, so selection pressure depends on the fitness scale
static void weights_roulette(const double* fits, double* weights, int n) {
  if (g_cfg.direction == MINIMIZE) {
    double max_fit = fits[0];
    for (int i = 1; i < n; i++)
      if (fits[i] > max_fit) max_fit = fits[i];
    for (int i = 0; i < n; i++) weights[i] = max_fit - fits[i] + 1.0;
  } else {
    double min_fit = fits[0];
    for (int i = 1; i < n; i++)
      if (fits[i] < min_fit) min_fit = fits[i];
    for (int i = 0; i < n; i++) weights[i] = fits[i] - min_fit + 1.0;
  }
}

// roulette_normalized: like roulette, but the shift past the worst individual is the
// population's average fitness spacing, eps = range / (n - 1), instead of 1 fitness unit
// Weights are then divided by the range, landing in [eps', 1 + eps'] with eps' = 1 / (n - 1)
// Pressure no longer depends on the fitness scale, and the worst individual keeps a small chance
static void weights_roulette_normalized(const double* fits, double* weights, int n) {
  double min_fit = fits[0], max_fit = fits[0];
  for (int i = 1; i < n; i++) {
    if (fits[i] < min_fit) min_fit = fits[i];
    if (fits[i] > max_fit) max_fit = fits[i];
  }
  double range = max_fit - min_fit;
  if (range <= 0.0) {
    for (int i = 0; i < n; i++) weights[i] = 1.0;
    return;
  }
  double eps = range / (n - 1);  // n >= 2 by validation
  for (int i = 0; i < n; i++) {
    double dist = g_cfg.direction == MINIMIZE ? max_fit - fits[i] : fits[i] - min_fit;
    weights[i]  = (dist + eps) / range;
  }
}

// rank: linear ranking (Baker) - individuals are sorted worst to best and weighted by position
// alone, w = (2 - s) + 2 (s - 1) r / (n - 1) for rank r in [0, n - 1], s = rank_pressure
// Weights average 1, so each is the individual's expected number of copies: s for the best,
// 2 - s for the worst
// Worst first, ties broken by index
static int cmp_worst_first(const void* pa, const void* pb) {
  const FitIdx* a = pa;
  const FitIdx* b = pb;
  if (a->fit != b->fit) return is_better(a->fit, b->fit) ? 1 : -1;
  return (a->idx > b->idx) - (a->idx < b->idx);
}

// `order` is caller-provided sort space (n entries)
static void weights_rank(const double* fits, double* weights, FitIdx* order, int n) {
  for (int i = 0; i < n; i++) order[i] = (FitIdx) {fits[i], i};
  qsort(order, n, sizeof(FitIdx), cmp_worst_first);

  const double s = cfg.rank_pressure;
  for (int r = 0; r < n; r++)
    weights[order[r].idx] = (2.0 - s) + 2.0 * (s - 1.0) * r / (n - 1);  // n >= 2 by validation
}

// Fitness-proportional draw over precomputed weights: pop_size spins, each copying the
// individual whose cumulative-weight interval contains the draw
static void spin(uint64_t* pop, GaWork* work, int n, int words) {
  const double* weights = work->weights;
  double total = 0.0;
  for (int i = 0; i < n; i++) total += weights[i];

  for (int i = 0; i < n; i++) {
    double r          = rng_next_double() * total;
    double cumulative = 0.0;
    int    pick       = n - 1;  // guard: only reachable through rounding at the very top
    for (int j = 0; j < n; j++) {
      cumulative += weights[j];
      if (r <= cumulative) {
        pick = j;
        break;
      }
    }
    memcpy(pop + i * words, work->aux + pick * words, words * sizeof(uint64_t));
    work->picked[i] = pick;
  }
}

// Copies the population into work->aux and evaluates it into work->fits
static void snapshot(const uint64_t* pop, GaWork* work) {
  int words = words_per_ind();
  memcpy(work->aux, pop, (size_t) cfg.pop_size * words * sizeof(uint64_t));
  for (int i = 0; i < cfg.pop_size; i++) work->fits[i] = eval(pop + i * words, work->tmp);
}

static void selection_wheel(uint64_t* pop, GaWork* work) {
  int n = cfg.pop_size;
  snapshot(pop, work);

  switch (cfg.selection) {
    case ROULETTE:
      if (g_cfg.interactive) printf("Selection: Roulette\n\n");
      weights_roulette(work->fits, work->weights, n);
      break;
    case ROULETTE_NORMALIZED:
      if (g_cfg.interactive) printf("Selection: Normalized roulette\n\n");
      weights_roulette_normalized(work->fits, work->weights, n);
      break;
    case RANK:
      if (g_cfg.interactive) printf("Selection: Rank\n\n");
      weights_rank(work->fits, work->weights, work->order, n);
      break;
    case TOURNAMENT: break;  // not a wheel selection; run_selection never routes it here
  }

  spin(pop, work, n, words_per_ind());
}

static void selection_tournament(uint64_t* pop, GaWork* work) {
  int n     = cfg.pop_size;
  int words = words_per_ind();
  int k     = cfg.tournament_size;

  if (g_cfg.interactive) printf("Selection: Tournament\n\n");
  snapshot(pop, work);

  for (int i = 0; i < n; i++) {
    int winner = rng_next_int(0, n);
    for (int j = 1; j < k; j++) {
      int challenger = rng_next_int(0, n);
      if (is_better(work->fits[challenger], work->fits[winner])) winner = challenger;
    }
    memcpy(pop + i * words, work->aux + winner * words, words * sizeof(uint64_t));
    work->picked[i] = winner;
  }
}

// --- Reproduction step ---

static void crossover(uint64_t* a, uint64_t* b) {
  switch (cfg.crossover) {
    case XOVER_N_POINT: crossover_n_point(a, b); break;
    case XOVER_UNIFORM: crossover_uniform(a, b); break;
  }
}

// Index of the best entry in fits[0..n) that isn't excluded
// Strict comparison, so ties go to the earliest entry
static int best_of(const double* fits, int n, int exclude) {
  int best = -1;
  for (int k = 0; k < n; k++) {
    if (k == exclude) continue;
    if (best < 0 || is_better(fits[k], fits[best])) best = k;
  }
  return best;
}

// Family competition (elitist recombination): slots `a` and `b` hold the two children, already
// crossed and mutated, and work->family[0..1] holds copies of their parents
// The best two of the four survive into the slots
// Children are listed first, so they win ties against parents, letting the search drift across
// plateaus instead of freezing on the incumbent
static void family_compete(uint64_t* a, uint64_t* b, double fit_pa, double fit_pb, int pair,
                           GaWork* work) {
  int       words  = words_per_ind();
  uint64_t* family = work->family;
  memcpy(family + 2 * words, a, words * sizeof(uint64_t));
  memcpy(family + 3 * words, b, words * sizeof(uint64_t));

  // Candidate order: child a, child b, parent a, parent b
  const int   src[4]   = {2, 3, 0, 1};
  const char* label[4] = {"child 1", "child 2", "parent 1", "parent 2"};
  double      fits[4]  = {eval(a, work->tmp), eval(b, work->tmp), fit_pa, fit_pb};
  int         first    = best_of(fits, 4, -1);
  int         second   = best_of(fits, 4, first);

  memcpy(a, family + src[first] * words, words * sizeof(uint64_t));
  memcpy(b, family + src[second] * words, words * sizeof(uint64_t));
  if (g_cfg.interactive)
    printf("  Family %d: kept %s and %s\n", pair, label[first], label[second]);
}

// Unpaired individual under family elitism: the mutant replaces its parent unless it's worse
static void family_compete_single(uint64_t* a, double fit_pa, int pair, GaWork* work) {
  if (is_better(fit_pa, eval(a, work->tmp))) {
    memcpy(a, work->family, words_per_ind() * sizeof(uint64_t));
    if (g_cfg.interactive) printf("  Family %d: kept parent 1\n", pair);
  } else if (g_cfg.interactive) {
    printf("  Family %d: kept child 1\n", pair);
  }
}

// Pairs adjacent individuals (0,1), (2,3), ... and applies crossover + per-bit mutation
// If pop_size is odd, the last individual is mutated but not crossed
// Under family elitism, each pair then competes with its parents (see family_compete)
// RNG draws happen in the same order with or without elitism: crossover, then mutation of each
// child, pair by pair
static void iter_pop(uint64_t* pop, GaWork* work) {
  int words  = words_per_ind();
  int family = cfg.elitism == ELITISM_FAMILY;

  for (int i = 0; i < cfg.pop_size; i += 2) {
    uint64_t* a = pop + i * words;

    if (i + 1 == cfg.pop_size) {
      if (family) memcpy(work->family, a, words * sizeof(uint64_t));
      mutation(a, i);
      if (family) family_compete_single(a, work->fits[work->picked[i]], i / 2, work);
      break;
    }

    uint64_t* b = a + words;
    if (family) memcpy(work->family, a, 2 * words * sizeof(uint64_t));
    crossover(a, b);
    mutation(a, i);
    mutation(b, i + 1);
    if (family)
      family_compete(a, b, work->fits[work->picked[i]], work->fits[work->picked[i + 1]], i / 2,
                     work);
  }
}

// --- Presentation (interactive only) ---

static void print_ind_bits(const uint64_t* ind) {
  for (int d = 0; d < g_cfg.n_dims; d++) {
    if (d > 0) printf(" ");
    printf("[");
    for (int b = cfg.ind_size - 1; b >= 0; b--) printf("%d", bit_get(ind, d * cfg.ind_size + b));
    printf("]");
  }
}

// `tmp` is the decode scratch (n_dims doubles)
static void print_pop(const uint64_t* pop, double* tmp) {
  int words    = words_per_ind();
  int best_idx = find_best(pop, tmp);

  for (int i = 0; i < cfg.pop_size; i++) {
    double fit = eval(pop + i * words, tmp);

    if (i == best_idx) printf(">> Individual %2d\n", i);
    else printf("   Individual %2d\n", i);

    printf("     bits: ");
    print_ind_bits(pop + i * words);
    printf("\n");

    printf("     x:    ");
    print_vec(tmp);
    printf("\n");

    printf("     fitness = %g\n\n", fit);
  }
}

// `tmp` is the decode scratch (n_dims doubles)
static void print_pairs(const uint64_t* pop, double* tmp) {
  int words = words_per_ind();

  for (int i = 0; i < cfg.pop_size; i++) {
    int is_odd_last = (i == cfg.pop_size - 1) && (cfg.pop_size % 2 != 0);

    if (is_odd_last) printf("  Pair %d  (no partner)\n", i / 2);
    else printf("  Pair %d\n", i / 2);

    double fit = eval(pop + i * words, tmp);
    printf("    Individual %d\n", i);
    printf("      bits: ");
    print_ind_bits(pop + i * words);
    printf("\n");
    printf("      x:    ");
    print_vec(tmp);
    printf("\n");
    printf("      fitness = %g\n", fit);

    if (is_odd_last) {
      printf("\n");
      break;
    }

    i++;
    fit = eval(pop + i * words, tmp);
    printf("    Individual %d\n", i);
    printf("      bits: ");
    print_ind_bits(pop + i * words);
    printf("\n");
    printf("      x:    ");
    print_vec(tmp);
    printf("\n");
    printf("      fitness = %g\n\n", fit);
  }
}

// --- Init ---

static void init_pop(uint64_t* pop) {
  int pop_size   = cfg.pop_size;
  int words      = words_per_ind();
  int total_bits = g_cfg.n_dims * cfg.ind_size;

  // calloc in ga_run already zeroed the buffer; just set the 1 bits
  for (int i = 0; i < pop_size; i++)
    for (int b = 0; b < total_bits; b++)
      if (rng_next_bit()) bit_set(pop + i * words, b, 1);
}

// --- Entry point ---

static void run_selection(uint64_t* pop, GaWork* work) {
  switch (cfg.selection) {
    case ROULETTE:
    case ROULETTE_NORMALIZED:
    case RANK:                selection_wheel(pop, work); break;
    case TOURNAMENT:          selection_tournament(pop, work); break;
  }
}

void ga_run(void) {
  int pop_size = cfg.pop_size;
  int words    = words_per_ind();

  uint64_t* pop = calloc((size_t) pop_size * (size_t) words, sizeof(uint64_t));
  if (!pop) die("memory allocation failed");
  GaWork  work = work_alloc();
  double* tmp  = work.tmp;

  init_pop(pop);

  if (g_cfg.interactive) {
    printf("Initial Population\n\n");
    print_pop(pop, tmp);
    wait_for_enter();
    print_separator();
  }

  for (int gen = 0; gen < cfg.generations; gen++) {
    if (g_cfg.interactive) {
      print_iteration_header(gen);
      run_selection(pop, &work);
      print_pairs(pop, tmp);
      wait_for_enter();
      print_separator();
      iter_pop(pop, &work);
      printf("Current Population\n\n");
      print_pop(pop, tmp);
      wait_for_enter();
      print_separator();
    } else {
      run_selection(pop, &work);
      iter_pop(pop, &work);
      double bf = eval(pop + find_best(pop, tmp) * words, tmp);
      print_iteration_line(gen, bf);
    }
  }

  // Final result: best of the final generation
  int    bi = find_best(pop, tmp);
  double bf = eval(pop + bi * words, tmp);  // leaves `tmp` holding the best position
  print_result(tmp, bf);

  work_free(&work);
  free(pop);
}
