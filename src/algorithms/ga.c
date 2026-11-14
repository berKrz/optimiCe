#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "algorithm.h"

// --- Config ---

typedef enum {
  SELECT_ROULETTE,
  SELECT_ROULETTE_NORMALIZED,
  SELECT_RANK,
  SELECT_TOURNAMENT
} SelectionKind;
typedef enum { XOVER_N_POINT, XOVER_UNIFORM } CrossoverKind;
typedef enum { ELITISM_NONE, ELITISM_FAMILY } ElitismKind;

typedef struct {
  int           pop_size;
  int           ind_size;  // bits per dimension (capped at 63 so 1ULL << ind_size is defined)
  int           tournament_size;
  double        rank_pressure;  // linear ranking: expected copies of the best, in [1.0, 2.0]
  DoubleList    cut_points;  // n_point cut ratios in (0.0, 1.0), increasing, applied per-dimension
  double        mutation_rate;  // per-bit flip probability in [0.0, 1.0)
  SelectionKind selection;
  CrossoverKind crossover;
  ElitismKind   elitism;
} GaConfig;

static _Thread_local GaConfig cfg;

// --- Field table ---

static const EnumEntry selection_table[] = {
    {"roulette",            SELECT_ROULETTE           },
    {"roulette_normalized", SELECT_ROULETTE_NORMALIZED},
    {"rank",                SELECT_RANK               },
    {"tournament",          SELECT_TOURNAMENT         },
    {NULL,                  0                         },
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

static const FieldSpec ga_fields[] = {
    FIELD(GaConfig, pop_size, INT, "15", "Population size (>= 2)"),
    FIELD(GaConfig, ind_size, INT, "22", "Bits per dimension [2, 63]"),
    FIELD(GaConfig, tournament_size, INT, "2", "Individuals per tournament"),
    FIELD(GaConfig, rank_pressure, DOUBLE, "1.5", "Rank selection pressure in [1.0, 2.0]"),
    FIELD(GaConfig, cut_points, DOUBLE_LIST, "[0.6]",
          "n_point cut ratios in (0.0, 1.0), increasing"),
    FIELD(GaConfig, mutation_rate, DOUBLE, "0.01", "Per-bit flip rate in [0.0, 1.0)"),
    ENUM_FIELD(GaConfig, selection, selection_table, "roulette", "Selection"),
    ENUM_FIELD(GaConfig, crossover, crossover_table, "n_point", "Crossover"),
    ENUM_FIELD(GaConfig, elitism, elitism_table, "none", "Elitism"),
    FIELDS_END,
};

static void* ga_cfg_base(void) { return &cfg; }

// --- Validate ---

// Defined with the encoding, under State
static int words_per_ind(void);
static int cut_index(double r);
static int init_levels(uint64_t* lo, uint64_t* hi);

// Each ratio must lie in (0, 1) and be strictly increasing, and the resulting cut indices must be
// distinct and >= 1, so every cut actually splits the slice
// A cut at index 0 only flips which segments are kept vs swapped, yielding the same pair of
// children, and two ratios landing on the same bit collapse into one cut
// Either way the user would silently get fewer cuts than asked for
static void validate_cut_points(void) {
  const DoubleList* c = &cfg.cut_points;
  if (c->n < 1) die_with_hint("cut_points must have at least one element");
  for (int i = 0; i < c->n; i++) {
    if (!(c->v[i] > 0.0 && c->v[i] < 1.0))
      die_with_hint("cut_points[%d] = %g must be in (0.0, 1.0)", i, c->v[i]);
    if (i > 0 && !(c->v[i] > c->v[i - 1]))
      die_with_hint("cut_points must be strictly increasing (%g after %g)", c->v[i], c->v[i - 1]);
    int idx = cut_index(c->v[i]);
    if (idx < 1)
      die_with_hint("cut_points[%d] = %g lands on bit 0 with ind_size = %d (needs >= %g)", i,
                    c->v[i], cfg.ind_size, 1.0 / cfg.ind_size);
    if (i > 0 && idx == cut_index(c->v[i - 1]))
      die_with_hint("cut_points %g and %g land on the same bit (%d) with ind_size = %d",
                    c->v[i - 1], c->v[i], idx, cfg.ind_size);
  }
}

static void ga_validate(void) {
  if (cfg.pop_size < 2) die_with_hint("pop_size must be >= 2");
  if (cfg.ind_size < 2 || cfg.ind_size > 63) die_with_hint("ind_size must be in [2, 63]");
  if (!isfinite(g_cfg.domain_min) || !isfinite(g_cfg.domain_max))
    die_with_hint("ga needs a finite domain: its bits decode onto [domain_min, domain_max]");
  // Bits are indexed with ints, and so are the words of the whole population
  check_fits_int(g_cfg.n_dims, cfg.ind_size, "n_dims x ind_size");
  check_fits_int(cfg.pop_size, words_per_ind(), "pop_size x n_dims x ind_size");
  uint64_t lo, hi;
  if (!init_levels(&lo, &hi))
    die_with_hint("the init range holds no value that ind_size bits can represent");
  if (!(cfg.mutation_rate >= 0.0 && cfg.mutation_rate < 1.0))
    die_with_hint("mutation_rate must be in [0.0, 1.0)");
  // tournament_size is ignored under other selections, so it's only checked when in use
  if (cfg.selection == SELECT_TOURNAMENT) {
    if (cfg.tournament_size < 2) die_with_hint("tournament_size must be >= 2");
    if (cfg.tournament_size > cfg.pop_size)
      die_with_hint("tournament_size must be at most pop_size (pop_size=%d, tournament_size=%d)",
                    cfg.pop_size, cfg.tournament_size);
  }
  // cut_points is only checked when in use
  if (cfg.crossover == XOVER_N_POINT) validate_cut_points();
  // rank_pressure is only checked when in use
  if (cfg.selection == SELECT_RANK && !(cfg.rank_pressure >= 1.0 && cfg.rank_pressure <= 2.0))
    die_with_hint("rank_pressure must be in [1.0, 2.0]");
}

// --- State ---

// Bit-packed individuals
// The population is one flat uint64_t[pop_size * words_per_ind]. Each
// individual gets n_dims * ind_size contiguous bits (little-endian within
// its own slice: bit b of dimension d lives at global index d * ind_size + b).
// Decoded dimensions come out as unsigned integers in [0, 2^ind_size - 1],
// then linearly mapped onto [domain_min, domain_max]

static int total_bits(void) { return g_cfg.n_dims * cfg.ind_size; }

static int words_per_ind(void) { return total_bits() / 64 + (total_bits() % 64 != 0); }

static inline int bit_get(const uint64_t* ind, int i) {
  return (int) ((ind[i / 64] >> (i % 64)) & 1ULL);
}

static inline void bit_set(uint64_t* ind, int i, int v) {
  uint64_t mask = 1ULL << (i % 64);
  if (v) ind[i / 64] |= mask;
  else ind[i / 64] &= ~mask;
}

static inline void bit_flip(uint64_t* ind, int i) { ind[i / 64] ^= (1ULL << (i % 64)); }

// Cut index of ratio r within a dimension's slice
static int cut_index(double r) { return (int) (cfg.ind_size * r); }

// A dimension's ind_size bits, read as an unsigned int, are its level: one of 2^ind_size evenly
// spaced values, level 0 at domain_min and the last at domain_max
// ind_size <= 63 guarantees 1ULL << ind_size stays well-defined
static uint64_t max_level(void) { return (1ULL << cfg.ind_size) - 1ULL; }

static double level_value(uint64_t level) {
  return g_cfg.domain_min +
         (double) level * (g_cfg.domain_max - g_cfg.domain_min) / (double) max_level();
}

// The levels that decode inside the init range: every level in [*lo, *hi]
// Returns 0 when there are none, i.e. when the range fits between two neighbouring levels
// Levels decode to increasing values, so each end is a binary search over [0, max_level() + 1),
// tested with level_value itself: "inside" means what decode_dim will later compute
static int init_levels(uint64_t* lo, uint64_t* hi) {
  // first: the lowest level at or above init_min (one past the last level if there is none)
  uint64_t first = 0, end = max_level() + 1;
  while (first < end) {
    uint64_t mid = first + (end - first) / 2;
    if (level_value(mid) >= g_cfg.init_min) end = mid;
    else first = mid + 1;
  }

  // past: the lowest level above init_max, so the last one inside is past - 1
  uint64_t past = 0;
  end           = max_level() + 1;
  while (past < end) {
    uint64_t mid = past + (end - past) / 2;
    if (level_value(mid) > g_cfg.init_max) end = mid;
    else past = mid + 1;
  }

  if (first >= past) return 0;
  *lo = first;
  *hi = past - 1;
  return 1;
}

static double decode_dim(const uint64_t* ind, int d) {
  int      base  = d * cfg.ind_size;
  uint64_t level = 0;
  for (int b = 0; b < cfg.ind_size; b++)
    if (bit_get(ind, base + b)) level |= (1ULL << b);
  return level_value(level);
}

static void decode(const uint64_t* ind, double* pos) {
  for (int d = 0; d < g_cfg.n_dims; d++) pos[d] = decode_dim(ind, d);
}

// The population and its fitness, plus the scratch buffers the steps need. Every buffer's size
// is fixed for the whole run, so they're allocated once
typedef struct {
  uint64_t* inds;  // pop_size * words: the individuals
  double*   fit;   // pop_size: fit[i] is the cached fitness of individual i

  // Selection
  uint64_t* parents;     // pop_size * words: the population as it was before selection
  double*   parent_fit;  // pop_size: its fitness
  int*      picked;      // pop_size: picked[i] = which parent selection put in slot i
  double*   weights;     // pop_size: wheel selections' weights
  Ranked*   order;       // pop_size: rank's sort buffer

  uint64_t* family;  // 4 * words: parents + children of the pair under family elitism
  double*   pos;     // n_dims: where an individual is decoded to
} Population;

static Population population_alloc(void) {
  size_t n = (size_t) cfg.pop_size, words = (size_t) words_per_ind();
  return (Population) {
      .inds       = xcalloc(n * words, sizeof(uint64_t)),  // zeroed: init only sets the 1 bits
      .fit        = xmalloc(n * sizeof(double)),
      .parents    = xmalloc(n * words * sizeof(uint64_t)),
      .parent_fit = xmalloc(n * sizeof(double)),
      .picked     = xmalloc(n * sizeof(int)),
      .weights    = xmalloc(n * sizeof(double)),
      .order      = xmalloc(n * sizeof(Ranked)),
      .family     = xmalloc(4 * words * sizeof(uint64_t)),
      .pos        = xmalloc((size_t) g_cfg.n_dims * sizeof(double)),
  };
}

static void population_free(Population* pop) {
  free(pop->pos);
  free(pop->family);
  free(pop->order);
  free(pop->weights);
  free(pop->picked);
  free(pop->parent_fit);
  free(pop->parents);
  free(pop->fit);
  free(pop->inds);
}

static uint64_t* individual(const Population* pop, int i) {
  return pop->inds + i * words_per_ind();
}

// --- Steps ---

// Decodes `ind` and returns its fitness
// The GA's only call to evaluate - every individual's fitness is cached in `fit`, next to the
// population, and this runs once per individual, when it's created: at init, and after crossover
// and mutation, everything else (selection, elitism, the progress line, the result, the
// interactive printers) reads the cache
static double evaluate_individual(const Population* pop, const uint64_t* ind) {
  decode(ind, pop->pos);
  return evaluate(pop->pos);
}

// Index of the population's current best individual, from the cached fitness
// Strict comparison, so ties go to the lowest index
static int find_best(const Population* pop) {
  int best = 0;
  for (int i = 1; i < cfg.pop_size; i++)
    if (is_better(pop->fit[i], pop->fit[best])) best = i;
  return best;
}

static void init_population(Population* pop) {
  uint64_t lo, hi;
  if (g_cfg.init_min == g_cfg.domain_min && g_cfg.init_max == g_cfg.domain_max) {
    // The init range is the whole domain: every bit pattern is inside it, so the bits are random
    for (int i = 0; i < cfg.pop_size; i++)
      for (int b = 0; b < total_bits(); b++)
        if (rng_next_bit()) bit_set(individual(pop, i), b, 1);
  } else {
    // A narrower init range: random bits would land anywhere in the domain, so each dimension
    // draws one of the levels that decode inside the range and gets that level's bits
    init_levels(&lo, &hi);  // never empty: ga_validate checked
    for (int i = 0; i < cfg.pop_size; i++) {
      for (int d = 0; d < g_cfg.n_dims; d++) {
        uint64_t level = lo + rng_next_uint64(hi - lo + 1);
        for (int b = 0; b < cfg.ind_size; b++)
          if ((level >> b) & 1ULL) bit_set(individual(pop, i), d * cfg.ind_size + b, 1);
      }
    }
  }

  for (int i = 0; i < cfg.pop_size; i++) pop->fit[i] = evaluate_individual(pop, individual(pop, i));
}

// Wheel weights: each turns raw fitness into non-negative weights that grow
// All of them fill `weights` (pop_size entries) from `fits` and are direction-aware

static void fitness_range(const double* fits, int n, double* min_fit, double* max_fit) {
  *min_fit = *max_fit = fits[0];
  for (int i = 1; i < n; i++) {
    if (fits[i] < *min_fit) *min_fit = fits[i];
    if (fits[i] > *max_fit) *max_fit = fits[i];
  }
}

// roulette: shift by the population's extreme so the worst individual gets weight 1
// Minimization uses (max - f) + 1, maximization (f - min) + 1
// The +1 is in fitness units, so selection pressure depends on the fitness scale
static void weights_roulette(const double* fits, double* weights, int n) {
  double min_fit, max_fit;
  fitness_range(fits, n, &min_fit, &max_fit);
  for (int i = 0; i < n; i++)
    weights[i] = (g_cfg.direction == MINIMIZE ? max_fit - fits[i] : fits[i] - min_fit) + 1.0;
}

// roulette_normalized: like roulette, but the shift past the worst individual is the
// population's average fitness spacing, eps = range / (n - 1), instead of 1 fitness unit
// Weights are then divided by the range, landing in [eps', 1 + eps'] with eps' = 1 / (n - 1)
// Pressure no longer depends on the fitness scale, and the worst individual keeps a small chance
static void weights_roulette_normalized(const double* fits, double* weights, int n) {
  double min_fit, max_fit;
  fitness_range(fits, n, &min_fit, &max_fit);
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
// `order` is caller-provided sort space (n entries)
static void weights_rank(const double* fits, double* weights, Ranked* order, int n) {
  rank_by_fitness(fits, n, order, RANK_WORST_FIRST);

  const double s = cfg.rank_pressure;
  for (int r = 0; r < n; r++)
    weights[order[r].index] = (2.0 - s) + 2.0 * (s - 1.0) * r / (n - 1);  // n >= 2 by validation
}

// Fitness-proportional draw over the weights: pop_size spins, each picking the parent whose
// cumulative-weight interval contains the draw
static void spin(Population* pop) {
  int           n       = cfg.pop_size;
  const double* weights = pop->weights;
  double        total   = 0.0;
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
    pop->picked[i] = pick;
  }
}

// Each slot goes to the best of tournament_size parents drawn at random, with repetition
static void tournament(Population* pop) {
  for (int i = 0; i < cfg.pop_size; i++) {
    int winner = rng_next_int(0, cfg.pop_size);
    for (int j = 1; j < cfg.tournament_size; j++) {
      int challenger = rng_next_int(0, cfg.pop_size);
      if (is_better(pop->parent_fit[challenger], pop->parent_fit[winner])) winner = challenger;
    }
    pop->picked[i] = winner;
  }
}

// Replaces the population with pop_size parents picked from it, by the configured method
// Every method only decides `picked`; the picked parents and their fitness are then copied into
// the slots, so no selected individual is evaluated again
static void select_parents(Population* pop) {
  size_t ind_bytes = (size_t) words_per_ind() * sizeof(uint64_t);
  int    n         = cfg.pop_size;

  if (g_opts.interactive) printf("Selection: %s\n\n", enum_name(selection_table, cfg.selection));

  memcpy(pop->parents, pop->inds, (size_t) n * ind_bytes);
  memcpy(pop->parent_fit, pop->fit, (size_t) n * sizeof(double));

  switch (cfg.selection) {
    case SELECT_ROULETTE:
      weights_roulette(pop->parent_fit, pop->weights, n);
      spin(pop);
      break;
    case SELECT_ROULETTE_NORMALIZED:
      weights_roulette_normalized(pop->parent_fit, pop->weights, n);
      spin(pop);
      break;
    case SELECT_RANK:
      weights_rank(pop->parent_fit, pop->weights, pop->order, n);
      spin(pop);
      break;
    case SELECT_TOURNAMENT: tournament(pop); break;
  }

  for (int i = 0; i < n; i++) {
    memcpy(individual(pop, i), pop->parents + pop->picked[i] * words_per_ind(), ind_bytes);
    pop->fit[i] = pop->parent_fit[pop->picked[i]];
  }
}

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
  for (int i = 0; i < total_bits(); i++)
    if (rng_next_bit()) swap_bit(a, b, i);
}

static void crossover(uint64_t* a, uint64_t* b) {
  switch (cfg.crossover) {
    case XOVER_N_POINT: crossover_n_point(a, b); break;
    case XOVER_UNIFORM: crossover_uniform(a, b); break;
  }
}

// Per-bit flip at `mutation_rate`
// In interactive mode, prints a per-individual header on the first flip and each flipped bit index
// underneath
static void mutate(uint64_t* ind, int index) {
  int header = 0;
  for (int i = 0; i < total_bits(); i++) {
    if (rng_next_double() < cfg.mutation_rate) {
      if (g_opts.interactive) {
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

// Family competition (elitist recombination): slots i and i + 1 hold the two children, already
// crossed and mutated, and pop->family[0..1] holds copies of their parents
// The best two of the four survive into the slots, with their fitness
// fit[i] and fit[i + 1] hold the parents' fitness on entry
// Children are listed first, so they win ties against parents, letting the search drift across
// plateaus instead of freezing on the incumbent
static void family_compete(Population* pop, int i) {
  size_t    ind_bytes = (size_t) words_per_ind() * sizeof(uint64_t);
  uint64_t* family    = pop->family;
  uint64_t* a         = individual(pop, i);
  uint64_t* b         = individual(pop, i + 1);
  memcpy(family + 2 * words_per_ind(), a, ind_bytes);
  memcpy(family + 3 * words_per_ind(), b, ind_bytes);

  // Candidate order: child a, child b, parent a, parent b
  const int   src[4]   = {2, 3, 0, 1};
  const char* label[4] = {"child 1", "child 2", "parent 1", "parent 2"};
  double      fits[4]  = {evaluate_individual(pop, a), evaluate_individual(pop, b), pop->fit[i],
                          pop->fit[i + 1]};
  int         first    = best_of(fits, 4, -1);
  int         second   = best_of(fits, 4, first);

  memcpy(a, family + src[first] * words_per_ind(), ind_bytes);
  memcpy(b, family + src[second] * words_per_ind(), ind_bytes);
  pop->fit[i]     = fits[first];
  pop->fit[i + 1] = fits[second];
  if (g_opts.interactive)
    printf("  Family %d: kept %s and %s\n", i / 2, label[first], label[second]);
}

// Unpaired individual under family elitism: the mutant in slot i replaces its parent unless it's
// worse
// fit[i] holds the parent's fitness on entry and the survivor's on return
static void family_compete_single(Population* pop, int i) {
  uint64_t* a         = individual(pop, i);
  double    fit_child = evaluate_individual(pop, a);
  if (is_better(pop->fit[i], fit_child)) {
    memcpy(a, pop->family, (size_t) words_per_ind() * sizeof(uint64_t));
    if (g_opts.interactive) printf("  Family %d: kept parent 1\n", i / 2);
  } else {
    pop->fit[i] = fit_child;
    if (g_opts.interactive) printf("  Family %d: kept child 1\n", i / 2);
  }
}

// Pairs adjacent individuals (0,1), (2,3), ... and applies crossover + per-bit mutation
// If pop_size is odd, the last individual is mutated but not crossed
// Under family elitism, each pair then competes with its parents (see family_compete)
// RNG draws happen in the same order with or without elitism: crossover, then mutation of each
// child, pair by pair
// On entry `fit` holds the selected parents' fitness; on return, that of whatever fills each slot.
// Every child is evaluated exactly once, whether or not crossover or mutation changed it
static void reproduce(Population* pop) {
  size_t ind_bytes = (size_t) words_per_ind() * sizeof(uint64_t);
  int    family    = cfg.elitism == ELITISM_FAMILY;

  for (int i = 0; i < cfg.pop_size; i += 2) {
    uint64_t* a = individual(pop, i);

    if (i + 1 == cfg.pop_size) {
      if (family) memcpy(pop->family, a, ind_bytes);
      mutate(a, i);
      if (family) family_compete_single(pop, i);
      else pop->fit[i] = evaluate_individual(pop, a);
      break;
    }

    uint64_t* b = individual(pop, i + 1);
    if (family) memcpy(pop->family, a, 2 * ind_bytes);
    crossover(a, b);
    mutate(a, i);
    mutate(b, i + 1);
    if (family) {
      family_compete(pop, i);
    } else {
      pop->fit[i]     = evaluate_individual(pop, a);
      pop->fit[i + 1] = evaluate_individual(pop, b);
    }
  }
}

// --- Interactive output ---

static void print_bits(const uint64_t* ind) {
  for (int d = 0; d < g_cfg.n_dims; d++) {
    if (d > 0) printf(" ");
    printf("[");
    for (int b = cfg.ind_size - 1; b >= 0; b--) printf("%d", bit_get(ind, d * cfg.ind_size + b));
    printf("]");
  }
}

// Individual i's bits, decoded values and cached fitness, one per line, behind `indent` spaces
static void print_individual(const Population* pop, int i, int indent) {
  decode(individual(pop, i), pop->pos);

  printf("%*sbits: ", indent, "");
  print_bits(individual(pop, i));
  printf("\n");

  printf("%*sx:    ", indent, "");
  print_vec(pop->pos);
  printf("\n");

  printf("%*sfitness = %g\n", indent, "", pop->fit[i]);
}

// The whole population, then a pause
static void show_population(const Population* pop) {
  int best = find_best(pop);

  for (int i = 0; i < cfg.pop_size; i++) {
    if (i == best) printf(">> Individual %2d\n", i);
    else printf("   Individual %2d\n", i);
    print_individual(pop, i, 5);
    printf("\n");
  }
  wait_for_enter();
  print_separator();
}

// The selected parents as the pairs reproduction will cross, then a pause
static void show_pairs(const Population* pop) {
  for (int i = 0; i < cfg.pop_size; i++) {
    int first = i % 2 == 0;
    int alone = first && i + 1 == cfg.pop_size;  // the last of an odd population

    if (alone) printf("  Pair %d  (no partner)\n", i / 2);
    else if (first) printf("  Pair %d\n", i / 2);

    printf("    Individual %d\n", i);
    print_individual(pop, i, 6);
    if (!first || alone) printf("\n");
  }
  wait_for_enter();
  print_separator();
}

// --- Entry point ---

static void ga_run(RunResult* out) {
  Population pop = population_alloc();
  init_population(&pop);

  if (g_opts.interactive) {
    printf("Initial Population\n\n");
    show_population(&pop);
  }

  StopState stop = stop_init();
  for (int gen = 0; gen < g_cfg.iterations; gen++) {
    if (g_opts.interactive) print_iteration_header(gen);
    select_parents(&pop);
    if (g_opts.interactive) show_pairs(&pop);

    reproduce(&pop);
    if (g_opts.interactive) {
      printf("Current Population\n\n");
      show_population(&pop);
    }

    // Best of this generation; it's what both the progress line and the early stop look at
    if (end_iteration(&stop, pop.fit[find_best(&pop)])) break;
  }

  // Final result: best of the final generation
  int best     = find_best(&pop);
  out->fitness = pop.fit[best];
  decode(individual(&pop, best), out->pos);
  out->stop = stop;

  population_free(&pop);
}

const Algorithm ga_algorithm = {
    .name     = "ga",
    .fields   = ga_fields,
    .cfg_base = ga_cfg_base,
    .cfg_size = sizeof(GaConfig),
    .validate = ga_validate,
    .run      = ga_run,
};
