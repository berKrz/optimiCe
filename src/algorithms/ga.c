#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "algorithms.h"
#include "common.h"

// --- Bit-packed individual layout ---

static int words_per_ind(void) {
  int total_bits = g_cfg.n_dims * g_cfg.ga.ind_size;
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

// --- Named tables for selection / crossover ---

static void selection_roulette(uint64_t* pop);
static void selection_tournament(uint64_t* pop);
static void crossover_single_point(uint64_t* a, uint64_t* b);

typedef struct {
  const char* name;
  void (*fn)(uint64_t*);
} SelectionEntry;

typedef struct {
  const char* name;
  void (*fn)(uint64_t*, uint64_t*);
} CrossoverEntry;

static const SelectionEntry selection_table[] = {
    {"roulette",   selection_roulette  },
    {"tournament", selection_tournament},
    {NULL,         NULL                },
};

static const CrossoverEntry crossover_table[] = {
    {"single_point", crossover_single_point},
    {NULL,           NULL                  },
};

static const char* selection_name_for(void (*fn)(uint64_t*)) {
  for (int i = 0; selection_table[i].name; i++)
    if (selection_table[i].fn == fn) return selection_table[i].name;
  return "(unknown)";
}

static const char* crossover_name_for(void (*fn)(uint64_t*, uint64_t*)) {
  for (int i = 0; crossover_table[i].name; i++)
    if (crossover_table[i].fn == fn) return crossover_table[i].name;
  return "(unknown)";
}

// --- Field handlers ---

static void apply_pop_size(const char* source, const char* key, const char* val) {
  char* end;
  errno  = 0;
  long v = strtol(val, &end, 10);
  if (*val == '\0' || *end != '\0' || errno == ERANGE || v < 2 || v > INT_MAX)
    die_at(source, key, "must be an integer >= 2");
  g_cfg.ga.pop_size = (int) v;
}

static void apply_ind_size(const char* source, const char* key, const char* val) {
  char* end;
  errno  = 0;
  long v = strtol(val, &end, 10);
  // Cap at 63 so (1ULL << ind_size) is well-defined. 2 is the minimum useful value.
  if (*val == '\0' || *end != '\0' || errno == ERANGE || v < 2 || v > 63)
    die_at(source, key, "must be an integer in [2, 63]");
  g_cfg.ga.ind_size = (int) v;
}

static void apply_generations(const char* source, const char* key, const char* val) {
  char* end;
  errno  = 0;
  long v = strtol(val, &end, 10);
  if (*val == '\0' || *end != '\0' || errno == ERANGE || v < 1 || v > INT_MAX)
    die_at(source, key, "must be a positive integer");
  g_cfg.ga.generations = (int) v;
}

static void apply_tournament_size(const char* source, const char* key, const char* val) {
  char* end;
  errno  = 0;
  long v = strtol(val, &end, 10);
  // Cross-field check (<= pop_size) runs in ga_validate; here we only enforce >= 2.
  if (*val == '\0' || *end != '\0' || errno == ERANGE || v < 2 || v > INT_MAX)
    die_at(source, key, "must be an integer >= 2");
  g_cfg.ga.tournament_size = (int) v;
}

static void apply_cut_point(const char* source, const char* key, const char* val) {
  char* end;
  errno    = 0;
  double v = strtod(val, &end);
  if (*val == '\0' || *end != '\0' || errno == ERANGE || !(v > 0.0 && v < 1.0))
    die_at(source, key, "must be a number in (0.0, 1.0)");
  g_cfg.ga.cut_point = v;
}

static void apply_mutation_rate(const char* source, const char* key, const char* val) {
  char* end;
  errno    = 0;
  double v = strtod(val, &end);
  if (*val == '\0' || *end != '\0' || errno == ERANGE || !(v >= 0.0 && v < 1.0))
    die_at(source, key, "must be a number in [0.0, 1.0)");
  g_cfg.ga.mutation_rate = v;
}

static void apply_selection(const char* source, const char* key, const char* val) {
  for (int i = 0; selection_table[i].name; i++) {
    if (strcmp(val, selection_table[i].name) == 0) {
      g_cfg.ga.selection_fn = selection_table[i].fn;
      return;
    }
  }
  die_at(source, key, "unknown selection method");
}

static void apply_crossover(const char* source, const char* key, const char* val) {
  for (int i = 0; crossover_table[i].name; i++) {
    if (strcmp(val, crossover_table[i].name) == 0) {
      g_cfg.ga.crossover_fn = crossover_table[i].fn;
      return;
    }
  }
  die_at(source, key, "unknown crossover method");
}

// --- Field table ---

const FieldSpec ga_fields[] = {
    {"pop_size",        FIELD_INT,    "15",           "Population size (>= 2)",           apply_pop_size       },
    {"ind_size",        FIELD_INT,    "22",           "Bits per dimension [2, 63]",       apply_ind_size       },
    {"generations",     FIELD_INT,    "30",           "Number of generations",            apply_generations    },
    {"tournament_size", FIELD_INT,    "2",            "Individuals per tournament",       apply_tournament_size},
    {"cut_point",       FIELD_DOUBLE, "0.6",          "Cut point ratio in (0.0, 1.0)",    apply_cut_point      },
    {"mutation_rate",   FIELD_DOUBLE, "0.01",         "Per-bit flip rate in [0.0, 1.0)",  apply_mutation_rate  },
    {"selection",       FIELD_FN,     "roulette",     "Selection: roulette | tournament", apply_selection      },
    {"crossover",       FIELD_FN,     "single_point", "Crossover: single_point",          apply_crossover      },
    {NULL,              0,            NULL,           NULL,                               NULL                 },
};

// --- Validate ---

void ga_validate(void) {
  if (g_cfg.ga.tournament_size > g_cfg.ga.pop_size) {
    char msg[160];
    snprintf(msg, sizeof(msg),
             "tournament_size must be at most pop_size (pop_size=%d, tournament_size=%d)",
             g_cfg.ga.pop_size, g_cfg.ga.tournament_size);
    die(msg);
  }
}

// --- Dump ---

void ga_dump(void) {
  printf("pop_size        = %d\n", g_cfg.ga.pop_size);
  printf("ind_size        = %d\n", g_cfg.ga.ind_size);
  printf("generations     = %d\n", g_cfg.ga.generations);
  printf("tournament_size = %d\n", g_cfg.ga.tournament_size);
  printf("cut_point       = %g\n", g_cfg.ga.cut_point);
  printf("mutation_rate   = %g\n", g_cfg.ga.mutation_rate);
  printf("selection       = %s\n", selection_name_for(g_cfg.ga.selection_fn));
  printf("crossover       = %s\n", crossover_name_for(g_cfg.ga.crossover_fn));
}

// --- Decode / eval ---

// Reads ind_size bits of dimension d as an unsigned int and linearly maps it onto [domain_min,
// domain_max]. ind_size <= 63 guarantees 1ULL << ind_size stays well-defined
static double decode_dim(const uint64_t* ind, int d) {
  int      base = d * g_cfg.ga.ind_size;
  uint64_t n    = 0;
  for (int b = 0; b < g_cfg.ga.ind_size; b++)
    if (bit_get(ind, base + b)) n |= (1ULL << b);
  double max_n = (double) ((1ULL << g_cfg.ga.ind_size) - 1ULL);
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

static int is_better(double a, double b) { return g_cfg.direction == MINIMIZE ? a < b : a > b; }

// Scans the population and returns the index of its current best individual
// Caller provides `tmp` (n_dims doubles) as the decode scratch buffer
static int find_best(const uint64_t* pop, double* tmp) {
  int    words    = words_per_ind();
  int    best_idx = 0;
  double best_fit = eval(pop, tmp);
  for (int i = 1; i < g_cfg.ga.pop_size; i++) {
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
  int cut = (int) (g_cfg.ga.ind_size * g_cfg.ga.cut_point);
  for (int d = 0; d < g_cfg.n_dims; d++) {
    int base = d * g_cfg.ga.ind_size;
    for (int i = cut; i < g_cfg.ga.ind_size; i++) {
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
  int total_bits = g_cfg.n_dims * g_cfg.ga.ind_size;
  int header     = 0;
  for (int i = 0; i < total_bits; i++) {
    if (rng_next_double() < g_cfg.ga.mutation_rate) {
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
  int pop_size = g_cfg.ga.pop_size;
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
  int pop_size = g_cfg.ga.pop_size;
  int words    = words_per_ind();
  int k        = g_cfg.ga.tournament_size;

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
  for (int i = 0; i < g_cfg.ga.pop_size; i++) {
    if (i != g_cfg.ga.pop_size - 1 && i % 2 == 0)
      g_cfg.ga.crossover_fn(pop + i * words, pop + (i + 1) * words);
    mutation(pop + i * words, i);
  }
}

// --- Presentation (interactive only) ---

static void print_ind_bits(const uint64_t* ind) {
  for (int d = 0; d < g_cfg.n_dims; d++) {
    if (d > 0) printf(" ");
    printf("[");
    for (int b = g_cfg.ga.ind_size - 1; b >= 0; b--)
      printf("%d", bit_get(ind, d * g_cfg.ga.ind_size + b));
    printf("]");
  }
}

static void print_vec(const double* v) {
  printf("[ ");
  for (int d = 0; d < g_cfg.n_dims; d++) printf("%7.3f ", v[d]);
  printf("]");
}

static void print_pop(const uint64_t* pop) {
  int     words = words_per_ind();
  double* tmp   = malloc(g_cfg.n_dims * sizeof(double));
  if (!tmp) die("memory allocation failed");
  int best_idx = find_best(pop, tmp);

  for (int i = 0; i < g_cfg.ga.pop_size; i++) {
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

static void print_pares(const uint64_t* pop) {
  int     words = words_per_ind();
  double* tmp   = malloc(g_cfg.n_dims * sizeof(double));
  if (!tmp) die("memory allocation failed");

  for (int i = 0; i < g_cfg.ga.pop_size; i++) {
    int is_odd_last = (i == g_cfg.ga.pop_size - 1) && (g_cfg.ga.pop_size % 2 != 0);

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
  int pop_size   = g_cfg.ga.pop_size;
  int words      = words_per_ind();
  int total_bits = g_cfg.n_dims * g_cfg.ga.ind_size;

  // calloc in ga_run already zeroed the buffer; just set the 1 bits
  for (int i = 0; i < pop_size; i++)
    for (int b = 0; b < total_bits; b++)
      if (rng_next_bit()) bit_set(pop + i * words, b, 1);
}

// --- Entry point ---

void ga_run(void) {
  int pop_size = g_cfg.ga.pop_size;
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

  for (int gen = 0; gen < g_cfg.ga.generations; gen++) {
    if (g_cfg.interactive) {
      print_iteration_header(gen);
      g_cfg.ga.selection_fn(pop);
      print_pares(pop);
      wait_for_enter();
      print_separator();
      iter_pop(pop);
      printf("Current Population\n\n");
      print_pop(pop);
      wait_for_enter();
      print_separator();
    } else {
      g_cfg.ga.selection_fn(pop);
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
