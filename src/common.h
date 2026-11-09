#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdnoreturn.h>

typedef struct Algorithm Algorithm;  // defined in algorithms.h
typedef struct FieldSpec FieldSpec;  // defined in algorithms.h

typedef enum { MINIMIZE, MAXIMIZE } OptDirection;

// Common config shared across all algorithms. Each algorithm's own parameters
// live in a file-scope `_Thread_local cfg` inside its own translation unit --
// private, out of the shared header and per-thread
typedef struct {
  uint32_t     seed;        // the job's seed while it runs; the base seed in a stored RunConfig
  int          replicates;  // [common]-only; load_batch copies it into the Batch
  OptDirection direction;
  int          n_dims;
  double       domain_min;
  double       domain_max;
  int          iterations;      // hard cap on every run
  double       target_fitness;  // early stop once the current best reaches it; NaN = off
  int          stagnation;      // early stop after this many iterations without improving; 0 = off

  double (*fitness_fn)(const double* pos);

  const Algorithm* algorithm;
} Config;

extern _Thread_local Config g_cfg;

// Process-wide options from the command line, not part of any config
// Set once by parse_args before anything runs, read-only afterwards, so shared by every thread
typedef struct {
  int interactive;
  int per_iteration;  // print a progress line per iteration; on only for single-job batches
} Options;

extern Options g_opts;

// When set, die() reports it as the error's location: the run being validated during loading, or
// the job a thread is running. Thread-local, so each thread names its own job
// Whoever sets it clears it before the buffer it points to goes away, so a later die() never reads
// a stale or dangling location
extern _Thread_local const char* g_where;

// A [[run]] after loading: its label and the images of both configs, resolved and validated
typedef struct {
  char   label[32];  // "pso-2": algorithm name + per-algorithm counter, by position in the file
  Config cfg;        // runner fields; cfg.seed holds the base seed
  void*  algo_cfg;   // cfg.algorithm->cfg_size bytes: the algorithm's cfg, post-validate
} RunConfig;

// Everything Phase 2 needs. Read-only once loaded, so threads can share it
// Job j is run j / replicates, replicate j % replicates, seeded with seed + replicate (common
// random numbers: replicate r gets the same seed in every run)
typedef struct {
  RunConfig* runs;
  int        n_runs;
  int        replicates;
  uint32_t   seed;            // base seed
  int        seed_generated;  // 1 when the config omitted seed
} Batch;

#define LINE_WIDTH 50

// utils.c
noreturn void die(const char* msg);
noreturn void die_at(const char* source, const char* key, const char* msg);

// malloc / calloc that die on failure
void* xmalloc(size_t size);
void* xcalloc(size_t n, size_t size);

void clear_screen(void);
void wait_for_enter(void);
void print_separator(void);
void print_iteration_header(int iter);
void print_iteration_line(int iter, double best_fitness);
// Early stopping: algorithms call stop_check once per iteration with their current best fitness
// and leave the loop when it returns 1
// The state tracks its own best-ever, so stagnation is measured the same way for every algorithm,
// whether or not its per-iteration best can regress
typedef enum { STOP_NONE, STOP_TARGET, STOP_STAGNATION } StopReason;

typedef struct {
  double     best;       // best-ever fitness seen by stop_check
  int        stale;      // consecutive iterations without improving `best`
  int        iteration;  // 0-based iteration the run stopped at, valid when reason != STOP_NONE
  StopReason reason;
} StopState;

StopState stop_init(void);
int       stop_check(StopState* st, int iter, double current_best);

// What a run hands back: its best fitness, where it was found, and how the run ended
// `pos` is owned by the caller and holds n_dims doubles; the algorithm copies into it
typedef struct {
  double    fitness;
  double*   pos;
  StopState stop;
} RunResult;

// Footer of a run; mentions an early stop when the result records one
void print_result(const RunResult* r);
void print_vec(const double* v);
int  is_better(double a, double b);

// rng.c
// Per-thread generator: rng_init seeds the calling thread's stream; rng_generate_seed picks a seed
// (time ^ pid) when the config gives none
uint32_t rng_generate_seed(void);
void     rng_init(uint32_t seed);
double   rng_next_double(void);
double   rng_next_double_range(double lo, double hi);
int      rng_next_int(int lo, int hi);  // half-open: [lo, hi)
int      rng_next_bit(void);

// config.c
void validate(void);
void dump_config(void);

// toml.c - Phase 1: parses, merges and validates every [[run]]; the TOML tree is freed on return
Batch load_batch(const char* filename);

// batch.c - Phase 2
int  batch_jobs(const Batch* ex);    // runs x replicates
void load_run(const RunConfig* rc);  // copies both config images into this thread's cfgs
void run_batch(const Batch* ex);
void dump_batch(const Batch* ex);
void free_batch(Batch* ex);

// args.c - CLI, loading, the --interactive check and --dump-config; returns only to run
Batch parse_args(int argc, char** argv);

// fitness.c
double sphere(const double* pos);
double rosenbrock(const double* pos);
double rastrigin(const double* pos);
double griewank(const double* pos);
double ackley(const double* pos);
double eggholder(const double* pos);
double schwefel(const double* pos);
double schaffer_f6(const double* pos);

typedef struct FitnessEntry {
  const char* name;
  double (*fn)(const double*);
  // n_dims_min == n_dims_max for fixed dimension functions
  int    n_dims_min;  // 0 = unbounded below
  int    n_dims_max;  // 0 = unbounded above
  double domain_lo;   // -INFINITY = unbounded below
  double domain_hi;   //  INFINITY = unbounded above
} FitnessEntry;

extern const FitnessEntry fitness_table[];
