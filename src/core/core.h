#pragma once

#include "algorithms/algorithm.h"

// --- Errors (error.c) ---

// When set, die and die_with_hint report it as where the error happened: the run being loaded,
// or the job a thread is running. Thread-local, so each thread names its own job
// Whoever sets it clears it before the string it points to goes away
extern _Thread_local const char* g_where;

// die_with_hint for a config file error, reported at `source` and, unless NULL, `key`
noreturn void die_at(const char* source, const char* key, const char* fmt, ...) PRINTF_LIKE(3, 4);

// --- Registry (registry.c) ---

// Every algorithm, NULL-terminated, in alphabetical order
extern const Algorithm* const algorithms[];

// Index of `name` in `algorithms[]`, or -1 when it isn't registered
int find_algorithm(const char* name);

// --- Fitness (fitness.c) ---

// The calling thread's count of evaluate calls; reset before a run and read after it
void     evaluations_reset(void);
uint64_t evaluations(void);

// A catalog entry by its config name (NULL when there's none), and its name
const FitnessEntry* fitness_find(const char* name);
const char*         fitness_name(const FitnessEntry* entry);
// Dies unless g_cfg's n_dims and domain are ones g_cfg.fitness is defined for
void fitness_check(void);
// Prints the catalog for --list: one line per function, its name and its limits
void fitness_print_catalog(void);

// --- Random numbers (rng.c) ---

// rng_init seeds the calling thread's stream; rng_generate_seed picks a seed (time ^ pid) for a
// config that gives none
uint32_t rng_generate_seed(void);
void     rng_init(uint32_t seed);

// --- Batch (batch.c) ---

// A [[run]] after loading: both of its configs, resolved and validated
typedef struct {
  char   label[32];  // "pso-2": the algorithm's name and the run's position among its runs
  Config cfg;        // runner fields; cfg.seed holds the base seed
  void*  algo_cfg;   // the algorithm's cfg: cfg.algorithm->cfg_size bytes
} RunConfig;

// Read-only once loaded, so threads can share it
// Job j is run j / replicates, replicate j % replicates, seeded with seed + replicate: replicate r
// gets the same seed in every run (common random numbers)
typedef struct {
  RunConfig* runs;
  int        n_runs;
  int        replicates;
  uint32_t   seed;            // base seed
  int        seed_generated;  // 1 when the config omitted seed
} Batch;

int batch_jobs(const Batch* batch);  // runs x replicates

// Runs every job and prints its result
// Jobs run in parallel on min(jobs, CPU cores) threads, but lines are printed in job order, so
// the output doesn't depend on which job finishes first
void run_batch(const Batch* batch);

// Prints the batch-wide values, then every run's resolved config
void dump_batch(const Batch* batch);

void free_batch(Batch* batch);
