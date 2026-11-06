#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "algorithms.h"
#include "common.h"

// --- GA-private config state ---

typedef enum { ROULETTE, TOURNAMENT } SelectionKind;
typedef enum { XOVER_SINGLE_POINT } CrossoverKind;

typedef struct {
  int           pop_size;
  int           ind_size;  // bits per dimension (capped at 63 so 1ULL << ind_size is defined)
  int           generations;
  int           tournament_size;
  double        cut_point;      // cut point ratio in (0.0, 1.0), applied per-dimension
  double        mutation_rate;  // per-bit flip probability in [0.0, 1.0)
  SelectionKind selection;
  CrossoverKind crossover;
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
    {"roulette",   ROULETTE  },
    {"tournament", TOURNAMENT},
    {NULL,         0         },
};

static const EnumEntry crossover_table[] = {
    {"single_point", XOVER_SINGLE_POINT},
    {NULL,           0                 },
};

// --- Field table ---

const FieldSpec ga_fields[] = {
    {"pop_size", FIELD_INT, offsetof(GaConfig, pop_size), NULL, "15", "Population size (>= 2)"},
    {"ind_size", FIELD_INT, offsetof(GaConfig, ind_size), NULL, "22", "Bits per dimension [2, 63]"},
    {"generations", FIELD_INT, offsetof(GaConfig, generations), NULL, "30",
     "Number of generations"},
    {"tournament_size", FIELD_INT, offsetof(GaConfig, tournament_size), NULL, "2",
     "Individuals per tournament"},
    {"cut_point", FIELD_DOUBLE, offsetof(GaConfig, cut_point), NULL, "0.6",
     "Cut point ratio in (0.0, 1.0)"},
    {"mutation_rate", FIELD_DOUBLE, offsetof(GaConfig, mutation_rate), NULL, "0.01",
     "Per-bit flip rate in [0.0, 1.0)"},
    {"selection", FIELD_ENUM, offsetof(GaConfig, selection), selection_table, "roulette",
     "Selection: roulette | tournament"},
    {"crossover", FIELD_ENUM, offsetof(GaConfig, crossover), crossover_table, "single_point",
     "Crossover: single_point"},
    {NULL, 0, 0, NULL, NULL, NULL},
};

void* ga_cfg_base(void) { return &cfg; }

// --- Validate ---

void ga_validate(void) {
  if (cfg.pop_size < 2) die("ga: pop_size must be >= 2");
  if (cfg.ind_size < 2 || cfg.ind_size > 63) die("ga: ind_size must be in [2, 63]");
  if (cfg.generations < 1) die("ga: generations must be positive");
  if (cfg.tournament_size < 2) die("ga: tournament_size must be >= 2");
  if (!(cfg.cut_point > 0.0 && cfg.cut_point < 1.0)) die("ga: cut_point must be in (0.0, 1.0)");
  if (!(cfg.mutation_rate >= 0.0 && cfg.mutation_rate < 1.0))
    die("ga: mutation_rate must be in [0.0, 1.0)");
  if (cfg.tournament_size > cfg.pop_size) {
    char msg[160];
    snprintf(msg, sizeof(msg),
             "ga: tournament_size must be at most pop_size (pop_size=%d, tournament_size=%d)",
             cfg.pop_size, cfg.tournament_size);
    die(msg);
  }
}

// --- Dump ---

static const char* selection_name(SelectionKind s) {
  for (int i = 0; selection_table[i].name; i++)
    if ((SelectionKind) selection_table[i].value == s) return selection_table[i].name;
  return "(unknown)";
}

static const char* crossover_name(CrossoverKind c) {
  for (int i = 0; crossover_table[i].name; i++)
    if ((CrossoverKind) crossover_table[i].value == c) return crossover_table[i].name;
  return "(unknown)";
}

void ga_dump(void) {
  printf("pop_size        = %d\n", cfg.pop_size);
  printf("ind_size        = %d\n", cfg.ind_size);
  printf("generations     = %d\n", cfg.generations);
  printf("tournament_size = %d\n", cfg.tournament_size);
  printf("cut_point       = %g\n", cfg.cut_point);
  printf("mutation_rate   = %g\n", cfg.mutation_rate);
  printf("selection       = %s\n", selection_name(cfg.selection));
  printf("crossover       = %s\n", crossover_name(cfg.crossover));
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

// Decodes `ind` into `tmp` and returns its fitness. `tmp` must be at least
// n_dims doubles; its contents are overwritten
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

// Dimension-aware single-point crossover: within each dimension's slice, bits
// [cut, ind_size) are swapped. The cut is the same ratio in every dimension
static void crossover_single_point(uint64_t* a, uint64_t* b) {
  int cut = (int) (cfg.ind_size * cfg.cut_point);
  for (int d = 0; d < g_cfg.n_dims; d++) {
    int base = d * cfg.ind_size;
    for (int i = cut; i < cfg.ind_size; i++) {
      int gi    = base + i;
      int bit_a = bit_get(a, gi);
      int bit_b = bit_get(b, gi);
      if (bit_a != bit_b) {
        bit_flip(a, gi);
        bit_flip(b, gi);
      }
    }
  }
}

// Per-bit flip at `mutation_rate`. In interactive mode, prints a per-individual
// header on the first flip and each flipped bit index underneath
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

// --- Selection ---

static void selection_roulette(uint64_t* pop) {
  int pop_size = cfg.pop_size;
  int words    = words_per_ind();

  double*   fits = malloc(pop_size * sizeof(double));
  uint64_t* aux  = malloc(pop_size * words * sizeof(uint64_t));
  double*   tmp  = malloc(g_cfg.n_dims * sizeof(double));
  if (!fits || !aux || !tmp) die("memory allocation failed");

  if (g_cfg.interactive) printf("Selection: Roulette\n\n");

  for (int i = 0; i < pop_size; i++) {
    memcpy(aux + i * words, pop + i * words, words * sizeof(uint64_t));
    fits[i] = eval(pop + i * words, tmp);
  }

  // Roulette needs a positive weight per individual that grows with "goodness"
  // Maximization: use fitness directly. Minimization: shift by (max - f) + 1 so
  // the worst individual gets weight 1 and better ones get higher weights.
  double total = 0.0;
  if (g_cfg.direction == MAXIMIZE) {
    for (int i = 0; i < pop_size; i++) total += fits[i];
  } else {
    double max_fit = fits[0];
    for (int i = 1; i < pop_size; i++)
      if (fits[i] > max_fit) max_fit = fits[i];
    for (int i = 0; i < pop_size; i++) {
      fits[i]  = max_fit - fits[i] + 1.0;
      total   += fits[i];
    }
  }

  for (int i = 0; i < pop_size; i++) {
    double r          = rng_next_double() * total;
    double cumulative = 0.0;
    for (int j = 0; j < pop_size; j++) {
      cumulative += fits[j];
      if (r <= cumulative) {
        memcpy(pop + i * words, aux + j * words, words * sizeof(uint64_t));
        break;
      }
    }
  }

  free(fits);
  free(aux);
  free(tmp);
}

static void selection_tournament(uint64_t* pop) {
  int pop_size = cfg.pop_size;
  int words    = words_per_ind();
  int k        = cfg.tournament_size;

  double*   fits = malloc(pop_size * sizeof(double));
  uint64_t* aux  = malloc(pop_size * words * sizeof(uint64_t));
  double*   tmp  = malloc(g_cfg.n_dims * sizeof(double));
  if (!fits || !aux || !tmp) die("memory allocation failed");

  if (g_cfg.interactive) printf("Selection: Tournament\n\n");

  for (int i = 0; i < pop_size; i++) {
    memcpy(aux + i * words, pop + i * words, words * sizeof(uint64_t));
    fits[i] = eval(pop + i * words, tmp);
  }

  for (int i = 0; i < pop_size; i++) {
    int winner = rng_next_int(0, pop_size);
    for (int j = 1; j < k; j++) {
      int challenger = rng_next_int(0, pop_size);
      if (is_better(fits[challenger], fits[winner])) winner = challenger;
    }
    memcpy(pop + i * words, aux + winner * words, words * sizeof(uint64_t));
  }

  free(fits);
  free(aux);
  free(tmp);
}

// --- Reproduction step ---

// Pairs adjacent individuals (0,1), (2,3), ... and applies crossover + per-bit
// mutation. If pop_size is odd, the last individual is mutated but not crossed
static void iter_pop(uint64_t* pop) {
  int words = words_per_ind();
  for (int i = 0; i < cfg.pop_size; i++) {
    if (i != cfg.pop_size - 1 && i % 2 == 0) {
      switch (cfg.crossover) {
        case XOVER_SINGLE_POINT:
          crossover_single_point(pop + i * words, pop + (i + 1) * words);
          break;
      }
    }
    mutation(pop + i * words, i);
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

static void print_pop(const uint64_t* pop) {
  int     words = words_per_ind();
  double* tmp   = malloc(g_cfg.n_dims * sizeof(double));
  if (!tmp) die("memory allocation failed");
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

  free(tmp);
}

static void print_pairs(const uint64_t* pop) {
  int     words = words_per_ind();
  double* tmp   = malloc(g_cfg.n_dims * sizeof(double));
  if (!tmp) die("memory allocation failed");

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

  free(tmp);
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

static void run_selection(uint64_t* pop) {
  switch (cfg.selection) {
    case ROULETTE:   selection_roulette(pop); break;
    case TOURNAMENT: selection_tournament(pop); break;
  }
}

void ga_run(void) {
  int pop_size = cfg.pop_size;
  int words    = words_per_ind();

  uint64_t* pop = calloc((size_t) pop_size * (size_t) words, sizeof(uint64_t));
  double*   tmp = malloc(g_cfg.n_dims * sizeof(double));
  if (!pop || !tmp) die("memory allocation failed");

  init_pop(pop);

  if (g_cfg.interactive) {
    printf("Initial Population\n\n");
    print_pop(pop);
    wait_for_enter();
    print_separator();
  }

  for (int gen = 0; gen < cfg.generations; gen++) {
    if (g_cfg.interactive) {
      print_iteration_header(gen);
      run_selection(pop);
      print_pairs(pop);
      wait_for_enter();
      print_separator();
      iter_pop(pop);
      printf("Current Population\n\n");
      print_pop(pop);
      wait_for_enter();
      print_separator();
    } else {
      run_selection(pop);
      iter_pop(pop);
      double bf = eval(pop + find_best(pop, tmp) * words, tmp);
      print_iteration_line(gen, bf);
    }
  }

  // Final result: best of the final generation
  int    bi = find_best(pop, tmp);
  double bf = eval(pop + bi * words, tmp);  // leaves `tmp` holding the best position
  print_result(tmp, bf);

  free(tmp);
  free(pop);
}
