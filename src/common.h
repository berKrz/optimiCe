#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdnoreturn.h>

typedef struct Algorithm    Algorithm;     // defined in algorithms.h
typedef struct FieldSpec    FieldSpec;     // defined in algorithms.h
typedef struct FitnessEntry FitnessEntry;  // opaque: defined in fitness.c

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
  double       init_min;        // range the initial population is drawn from, inside the domain
  double       init_max;        // NaN until validate resolves an unset end to the domain's
  int          iterations;      // hard cap on every run
  double       target_fitness;  // early stop once the current best reaches it; NaN = off
  int          stagnation;      // early stop after this many iterations without improving; 0 = off

  const FitnessEntry* fitness;  // catalog entry; only evaluate calls its function
  const Algorithm*    algorithm;
} Config;

extern _Thread_local Config g_cfg;

// Process-wide options from the command line, not part of any config
// Set once by parse_args before anything runs, read-only afterwards, so shared by every thread
typedef struct {
  int interactive;
  int per_iteration;  // print a progress line per iteration; on only for single-job batches
} Options;

extern Options g_opts;

// When set, die and die_with_hint report it as the error's location: the run being validated
// during loading, or the job a thread is running. Thread-local, so each thread names its own job
// Whoever sets it clears it before the buffer it points to goes away, so a later die never reads
// a stale or dangling location
extern _Thread_local const char* g_where;

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

#define LINE_WIDTH 50

// utils.c
// The die family takes a printf-style format, checked against its arguments by the compiler
#define PRINTF_LIKE(fmt, args) __attribute__((format(printf, fmt, args)))

// die is for failures while running (out of memory, a file that can't be written) and for
// internal errors. die_with_hint and die_at are for what the user wrote, in the config file or
// on the command line: they add a line pointing at --help
noreturn void die(const char* fmt, ...) PRINTF_LIKE(1, 2);
noreturn void die_with_hint(const char* fmt, ...) PRINTF_LIKE(1, 2);
noreturn void die_at(const char* source, const char* key, const char* fmt, ...) PRINTF_LIKE(3, 4);

// malloc / calloc that die on failure
void* xmalloc(size_t size);
void* xcalloc(size_t n, size_t size);

// Dies unless count x per_item fits in an int. Algorithms index their flat buffers with ints, so
// each one's validate checks its largest buffer here; `what` names the product in the message,
// e.g. "swarm_size x n_dims". Both factors must be positive
void check_fits_int(int count, long long per_item, const char* what);

void clear_screen(void);
void wait_for_enter(void);
void print_separator(void);
void print_iteration_header(int iter);
void print_iteration_line(int iter, double best_fitness);

// Writes the shortest %g form of x, at 6 to 17 significant digits, that reads back as exactly x
// Used for final results, so a printed value identifies the double; starting at 6 means it
// matches plain %g whenever %g was already exact
#define FMT_EXACT_LEN 32
void fmt_exact(char* buf, double x);
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
  int       iterations;   // iterations the run completed, early stop or not; set by the runner
  uint64_t  evaluations;  // fitness evaluations the run made (see evaluate); set by the runner
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
uint64_t rng_next_uint64(uint64_t n);   // half-open: [0, n), n > 0
int      rng_next_bit(void);

// config.c
void validate(void);
void dump_config(void);

// toml.c - parses, merges and validates every [[run]]; the TOML tree is freed on return
Batch load_batch(const char* filename);

// output.c - the --output results file
// output_open picks the path from `arg` (NULL: a generated name in the current directory; a
// trailing slash or an existing directory: a generated name inside it; anything else: that file),
// creates missing directories, prints the path to stderr and writes the header and column row;
// output_row writes one job's row
// Every write is checked: a failure dies naming the file
// Without output_open, the others do nothing
void output_open(const Batch* batch, const char* config_path, const char* arg);
void output_row(const char* label, int replicate, uint32_t seed, int n_dims, const RunResult* res);
void output_flush(void);
void output_close(void);

// batch.c
int  batch_jobs(const Batch* ex);    // runs x replicates
void load_run(const RunConfig* rc);  // copies both config images into this thread's cfgs
void run_batch(const Batch* ex);
void dump_batch(const Batch* ex);
void free_batch(Batch* ex);

// args.c - CLI, loading, the --interactive check and --dump-config; returns only to run
Batch parse_args(int argc, char** argv);

// fitness.c
// evaluate is the only way to call the run's fitness function (g_cfg.fitness), and it counts the
// call on this thread: the functions, and the catalog entries that point at them, are private to
// fitness.c. Every call counts, so the count is the run's cost only if algorithms never
// re-evaluate a solution whose fitness they already know, and never evaluate to print (otherwise
// --interactive would change it)
double   evaluate(const double* pos);
void     evaluations_reset(void);
uint64_t evaluations(void);

// What the rest of the program may do with a catalog entry: find it by its config name (NULL
// when there's no such function), name it, and check the run against its limits
const FitnessEntry* fitness_find(const char* name);
const char*         fitness_name(const FitnessEntry* entry);
// Dies unless g_cfg's n_dims and domain are ones g_cfg.fitness is defined for
void fitness_check(void);
