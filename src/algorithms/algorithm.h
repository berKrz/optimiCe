#pragma once

// The contract between the runner and an algorithm: everything an algorithm's file may use, and
// the one project header it includes
// To add an algorithm, create src/algorithms/<name>.c defining `const Algorithm <name>_algorithm`;
// the build compiles and registers it. The three files beside this one are worked examples

#include <stddef.h>
#include <stdint.h>
#include <stdnoreturn.h>

typedef struct Algorithm    Algorithm;     // defined below
typedef struct FieldSpec    FieldSpec;     // defined below
typedef struct FitnessEntry FitnessEntry;  // opaque: defined in core/fitness.c

// --- The run's configuration ---

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
  double       init_min;  // range the initial population is drawn from, inside the domain
  double       init_max;  // NaN until validate resolves an unset end to the domain's
  int          iterations;      // hard cap on every run
  double       target_fitness;  // early stop once the current best reaches it; NaN = off
  int          stagnation;      // early stop after this many iterations without improving; 0 = off

  const FitnessEntry* fitness;  // catalog entry; only evaluate calls its function
  const Algorithm*    algorithm;
} Config;

extern _Thread_local Config g_cfg;

// Process-wide options, not part of any config: what the code running a job reads
// Set once before anything runs (interactive by parse_args, per_iteration by main once the batch
// is loaded), read-only afterwards, so shared by every thread
typedef struct {
  int interactive;
  int per_iteration;  // print a progress line per iteration; on only for single-job batches
} Options;

extern Options g_opts;

// --- Errors and memory ---

// The die family takes a printf-style format, checked against its arguments by the compiler
#define PRINTF_LIKE(fmt, args) __attribute__((format(printf, fmt, args)))

// die is for failures while running (out of memory, a file that can't be written) and for
// internal errors. die_with_hint is for what the user wrote, in the config file or on the command
// line: it adds a line pointing at --help
noreturn void die(const char* fmt, ...) PRINTF_LIKE(1, 2);
noreturn void die_with_hint(const char* fmt, ...) PRINTF_LIKE(1, 2);

// malloc / calloc that die on failure
void* xmalloc(size_t size);
void* xcalloc(size_t n, size_t size);

// Dies unless count x per_item fits in an int. Algorithms index their flat buffers with ints, so
// each one's validate checks its largest buffer here; `what` names the product in the message,
// e.g. "swarm_size x n_dims". Both factors must be positive
void check_fits_int(int count, long long per_item, const char* what);

// --- Fitness ---

// evaluate is the only way to call the run's fitness function (g_cfg.fitness), and it counts the
// call on this thread: the functions, and the catalog entries that point at them, are private to
// fitness.c. Every call counts, so the count is the run's cost only if algorithms never
// re-evaluate a solution whose fitness they already know, and never evaluate to print (otherwise
// --interactive would change it)
double evaluate(const double* pos);

// Whether fitness `a` beats `b` in the run's direction
int is_better(double a, double b);

// Ranking a population by fitness without moving it: an array of (fitness, index) pairs is sorted
// in its place, and the population is then read through the indices
typedef struct {
  double fitness;
  int    index;
} Ranked;

typedef enum { RANK_BEST_FIRST, RANK_WORST_FIRST } RankOrder;

// Fills order[0..n) from `fits` and sorts it; equal fitness goes by index, lowest first
void rank_by_fitness(const double* fits, int n, Ranked* order, RankOrder direction);

// --- Random numbers ---

// Draws from the calling thread's random stream, which the runner seeds before each run
double   rng_next_double(void);
double   rng_next_double_range(double lo, double hi);
int      rng_next_int(int lo, int hi);  // half-open: [lo, hi)
uint64_t rng_next_uint64(uint64_t n);   // half-open: [0, n), n > 0
int      rng_next_bit(void);

// --- Stopping and the result ---

// Early stopping: an algorithm calls end_iteration once at the end of every iteration, with its
// current best fitness, and leaves its loop when it returns 1. The call also prints the
// iteration's progress line when those are on, so algorithms don't
// Stagnation is judged on the best-ever fitness, tracked here, so it works the same for every
// algorithm whether or not its per-iteration best can regress
typedef enum { STOP_NONE, STOP_TARGET, STOP_STAGNATION } StopReason;

typedef struct {
  double     best;        // best-ever fitness seen by end_iteration
  int        stale;       // consecutive iterations without improving `best`
  int        iterations;  // iterations completed so far; the run's total once it's over
  StopReason reason;
} StopState;

StopState stop_init(void);
int       end_iteration(StopState* st, double current_best);

// What a run hands back: its best fitness, where it was found, and how the run ended
// `pos` is owned by the caller and holds n_dims doubles; the algorithm copies into it
typedef struct {
  double    fitness;
  double*   pos;
  StopState stop;
  uint64_t  evaluations;  // fitness evaluations the run made (see evaluate); set by the runner
} RunResult;

// --- Interactive output ---

#define LINE_WIDTH 50

void wait_for_enter(void);
void print_separator(void);
void print_iteration_header(int iter);
void print_vec(const double* v);

// --- Config fields ---

// User-facing type label for config keys, it describes what shape of value the user may write in a
// config file, and feeds the --help printer and future validators.
typedef enum {
  FIELD_INT,
  FIELD_UINT,
  FIELD_DOUBLE,
  FIELD_BOOL,
  FIELD_ENUM,
  FIELD_FITNESS,
  FIELD_ALGORITHM,
  FIELD_DOUBLE_LIST,  // TOML array of numbers, stored as a DoubleList
} FieldType;

// Storage for FIELD_DOUBLE_LIST. Fixed capacity so the owning config struct stays flat (no
// allocation, copyable by value). The walker rejects arrays longer than DOUBLE_LIST_MAX
#define DOUBLE_LIST_MAX 64

typedef struct {
  int    n;
  double v[DOUBLE_LIST_MAX];
} DoubleList;

// Config sections, one bit each
// The walker is always in exactly one section; a field's scope is the OR of the sections it may
// appear in. --help groups fields by scope, and the walker rejects a key whose scope lacks the
// current section's bit
typedef enum {
  SECTION_COMMON   = 1 << 0,  // [common]
  SECTION_RUN      = 1 << 1,  // [[run]]
  SECTION_DEFAULTS = 1 << 2,  // [defaults.<algo>]
} Section;

// Named scopes for the masks most rows share
enum {
  SCOPE_GLOBAL = SECTION_COMMON | SECTION_RUN,    // a [[run]] value overrides [common]
  SCOPE_ALGO   = SECTION_RUN | SECTION_DEFAULTS,  // every algorithm field
};

// Pure data row. Walker reads `type` to decide how to parse a datum, consults
// `lookup` for FIELD_ENUM, writes the result through
// `base + offset`, where `base` is &g_cfg for runner_fields and the algorithm's cfg_base() for its
// own fields
typedef struct FieldSpec {
  const char* name;
  FieldType   type;
  unsigned    scope;            // OR of the Sections this field may appear in
  size_t      offset;           // relative to the table's base
  const void* lookup;           // FIELD_ENUM table, NULL otherwise
  const char* default_display;  // parsed back through the walker in the defaults pass
  const char* description;
} FieldSpec;

// Row constructors. A key is named after the struct member it's stored in, so the name is
// written once and the two can't disagree:
//   FIELD(PsoConfig, swarm_size, INT, "30", "Swarm size")
// FIELD_ROW spells out every column, for the runner's table, whose rows differ in scope
// FIELD and ENUM_FIELD are for an algorithm's table: every algorithm field has the same scope, and
// only an enum has a lookup table
#define FIELD_ROW(cfg_type, member, kind, scope, lookup, default_display, description) \
  {#member, FIELD_##kind, scope, offsetof(cfg_type, member), lookup, default_display, description}
#define FIELD(cfg_type, member, kind, default_display, description) \
  FIELD_ROW(cfg_type, member, kind, SCOPE_ALGO, NULL, default_display, description)
#define ENUM_FIELD(cfg_type, member, lookup, default_display, description) \
  FIELD_ROW(cfg_type, member, ENUM, SCOPE_ALGO, lookup, default_display, description)
#define FIELDS_END {NULL, 0, 0, 0, NULL, NULL, NULL}

// Walker iterates it reading `name`, and writes `value` (as int) through the FieldSpec's
// offset on a match Sentinel: {NULL, 0} terminates the table
typedef struct {
  const char* name;
  int         value;
} EnumEntry;

// --- Bounds ---

// Out-of-domain handling shared by continuous-position algorithms
// clamp pins the coordinate to the violated bound, reflect mirrors it back inside (bouncing as
// many times as needed), random redraws it uniformly in the domain
// When a velocity is given (v != NULL), clamp and random zero it and reflect flips its sign on an
// odd number of bounces
// An infinite end of the domain is never violated, so the mode only matters at a finite one
typedef enum { BOUNDS_CLAMP, BOUNDS_REFLECT, BOUNDS_RANDOM } BoundsMode;

extern const EnumEntry bounds_table[];

// Brings one coordinate back into [domain_min, domain_max]; no-op when already inside
// A non-finite coordinate is always brought back to a finite value, even in an unbounded domain
// `v` is the matching velocity component, or NULL for algorithms without one
void apply_bounds(double* x, double* v, BoundsMode mode);

// Dies when `mode` can't work on the run's domain: random on a domain with one infinite end
// Called from each algorithm's validate, after the runner has validated the domain
void check_bounds_mode(BoundsMode mode);

// --- The algorithm itself ---

// One per algorithm, defined in the algorithm's own file
// That file is all an algorithm needs: src/algorithms/<name>.c defining
// `const Algorithm <name>_algorithm` is compiled and registered by the build
typedef struct Algorithm {
  const char*      name;
  const FieldSpec* fields;
  void* (*cfg_base)(void);  // returns the address of the algo's _Thread_local cfg

  // sizeof the algorithm's cfg. The cfg is copied with memcpy, so it must hold its data by value:
  // no pointers to owned, mutable or parse-tree memory (DoubleList is the pattern); pointers to
  // static constants and functions are fine
  size_t cfg_size;

  // Called once per run, after every field is set and before anything reads the cfg
  // May resolve derived values (e.g. pso's w_end = w), so dump and run see the final config
  void (*validate)(void);

  // Fills `out`: fitness, stop state, and n_dims doubles into the caller-owned out->pos
  // Prints only its per-iteration / interactive output; the caller prints the footer
  void (*run)(RunResult* out);
} Algorithm;
