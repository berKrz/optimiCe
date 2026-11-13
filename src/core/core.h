#pragma once

// What the rest of the runner needs from core/, beyond what algorithms already get

#include "algorithms/algorithm.h"

// --- Errors (error.c) ---

// When set, die and die_with_hint report it as the error's location: the run being validated
// during loading, or the job a thread is running. Thread-local, so each thread names its own job
// Whoever sets it clears it before the buffer it points to goes away, so a later die never reads
// a stale or dangling location
extern _Thread_local const char* g_where;

// die_with_hint for an error in a config file: reported at `source` (and `key`, unless NULL)
noreturn void die_at(const char* source, const char* key, const char* fmt, ...) PRINTF_LIKE(3, 4);

// --- Registry (registry.c) ---

// Every algorithm, NULL-terminated, in alphabetical order
extern const Algorithm* const algorithms[];

// Index of `name` in `algorithms[]`, or -1 when it isn't registered
int find_algorithm(const char* name);

// --- Fitness (fitness.c) ---

// The evaluations counter behind evaluate, per thread; the runner resets it before a run and
// reads it after
void     evaluations_reset(void);
uint64_t evaluations(void);

// What the rest of the program may do with a catalog entry: find it by its config name (NULL
// when there's no such function), name it, and check the run against its limits
const FitnessEntry* fitness_find(const char* name);
const char*         fitness_name(const FitnessEntry* entry);
// Dies unless g_cfg's n_dims and domain are ones g_cfg.fitness is defined for
void fitness_check(void);
// Prints the catalog for --list: one line per function, its name and its limits
void fitness_print_catalog(void);

// --- Random numbers (rng.c) ---

// Per-thread generator: rng_init seeds the calling thread's stream; rng_generate_seed picks a seed
// (time ^ pid) when the config gives none
uint32_t rng_generate_seed(void);
void     rng_init(uint32_t seed);

// --- Batch (batch.c) ---

// A [[run]] after loading: its label and the images of both configs, resolved and validated
typedef struct {
  char   label[32];  // "pso-2": algorithm name + per-algorithm counter, by position in the file
  Config cfg;        // runner fields; cfg.seed holds the base seed
  void*  algo_cfg;   // cfg.algorithm->cfg_size bytes: the algorithm's cfg, post-validate
} RunConfig;

// Read-only once loaded, so threads can share it
// Job j is run j / replicates, replicate j % replicates, seeded with seed + replicate (common
// random numbers: replicate r gets the same seed in every run)
typedef struct {
  RunConfig* runs;
  int        n_runs;
  int        replicates;
  uint32_t   seed;            // base seed
  int        seed_generated;  // 1 when the config omitted seed
} Batch;

int  batch_jobs(const Batch* batch);  // runs x replicates
void run_batch(const Batch* batch);
void dump_batch(const Batch* batch);
void free_batch(Batch* batch);
