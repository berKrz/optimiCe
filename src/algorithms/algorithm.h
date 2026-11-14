#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdnoreturn.h>

typedef struct Algorithm    Algorithm;     // defined below
typedef struct FieldSpec    FieldSpec;     // defined below
typedef struct FitnessEntry FitnessEntry;  // opaque: defined in fitness.c

// --- The run's configuration ---

typedef enum { MINIMIZE, MAXIMIZE } OptDirection;

// The config every algorithm shares. An algorithm's own parameters live in a `_Thread_local cfg`
// private to its file
// seed and replicates are batch-wide, but sit here because the field tables write through this
// struct; load_batch copies both into the Batch
typedef struct {
  uint32_t     seed;        // while a job runs, that job's seed
  int          replicates;  // unused once loaded
  OptDirection direction;
  int          n_dims;
  double       domain_min;
  double       domain_max;
  double       init_min;        // range the initial population is drawn from, inside the domain
  double       init_max;        // NaN until validate resolves an unset end to the domain's
  int          iterations;      // hard cap on every run
  double       target_fitness;  // stop once the best reaches it; NaN = off
  int          stagnation;      // stop after this many iterations without improving; 0 = off

  const FitnessEntry* fitness;  // catalog entry; only evaluate calls its function
  const Algorithm*    algorithm;
} Config;

extern _Thread_local Config g_cfg;

// Process-wide options, not part of any config
// Set before anything runs and read-only afterwards, so every thread can share them
typedef struct {
  int interactive;    // --interactive; set by parse_args
  int per_iteration;  // print a progress line per iteration; set by main, for single-job batches
} Options;

extern Options g_opts;

// --- Errors and memory ---

// Makes the compiler check a printf-style format against its arguments
#define PRINTF_LIKE(fmt, args) __attribute__((format(printf, fmt, args)))

// die is for runtime failures (out of memory, an unwritable file) and internal errors
// die_with_hint is for errors in what the user wrote: it adds a line pointing at --help
noreturn void die(const char* fmt, ...) PRINTF_LIKE(1, 2);
noreturn void die_with_hint(const char* fmt, ...) PRINTF_LIKE(1, 2);

// malloc / calloc that die on failure
void* xmalloc(size_t size);
void* xcalloc(size_t n, size_t size);

// Dies unless count x per_item fits in an int; both must be positive
// Algorithms index their buffers with ints, so each validate checks its largest buffer here
// `what` names the product in the message, e.g. "swarm_size x n_dims"
void check_fits_int(int count, long long per_item, const char* what);

// --- Fitness ---

// The only way to call the run's fitness function; every call is counted
// The count is the run's reported cost, so an algorithm must not re-evaluate a solution whose
// fitness it already knows, nor evaluate just to print
double evaluate(const double* pos);

// Whether fitness `a` beats `b` in the run's direction
int is_better(double a, double b);

// For ranking a population without moving it: the pairs are sorted, and the population is read
// through their indices
typedef struct {
  double fitness;
  int    index;
} Ranked;

typedef enum { RANK_BEST_FIRST, RANK_WORST_FIRST } RankOrder;

// Fills order[0..n) from `fits` and sorts it; equal fitness goes by index, lowest first, so the
// order never depends on the sorting algorithm
void rank_by_fitness(const double* fits, int n, Ranked* order, RankOrder direction);

// --- Random numbers ---

// Draws from the calling thread's stream, which the runner seeds before each run
double   rng_next_double(void);
double   rng_next_double_range(double lo, double hi);
int      rng_next_int(int lo, int hi);  // half-open: [lo, hi)
uint64_t rng_next_uint64(uint64_t n);   // half-open: [0, n), n > 0
int      rng_next_bit(void);

// --- Stopping and the result ---

typedef enum { STOP_NONE, STOP_TARGET, STOP_STAGNATION } StopReason;

typedef struct {
  double     best;        // best fitness seen by end_iteration over the whole run
  int        stale;       // consecutive iterations without improving `best`
  int        iterations;  // iterations completed so far
  StopReason reason;
} StopState;

StopState stop_init(void);

// Called once at the end of every iteration with the algorithm's current best fitness; the
// algorithm leaves its loop when it returns 1
// Also prints the iteration's progress line, when those are on
// Stagnation is judged on `best`, so it works the same whether or not an algorithm's
// per-iteration best can get worse
int end_iteration(StopState* st, double current_best);

// What a run hands back
typedef struct {
  double    fitness;      // best fitness found
  double*   pos;          // where: n_dims doubles, owned by the caller
  StopState stop;         // how the run ended
  uint64_t  evaluations;  // calls to evaluate; set by the runner
} RunResult;

// --- Interactive output ---

// What an algorithm prints under --interactive is its own; these are the shared pieces

#define LINE_WIDTH 50

void wait_for_enter(void);              // blocks until the user presses Enter
void print_separator(void);             // a dashed rule, LINE_WIDTH wide
void print_iteration_header(int iter);  // "=== Iteration N ===", padded to LINE_WIDTH
void print_vec(const double* v);        // n_dims values as "[ a b ... ]", no newline

// --- Config fields ---

// What kind of value a config key takes: decides how it's parsed, shown in --help and dumped
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

// Storage for FIELD_DOUBLE_LIST. The capacity is fixed so a config struct holding one can be
// copied by value; longer arrays are rejected
#define DOUBLE_LIST_MAX 64

typedef struct {
  int    n;
  double v[DOUBLE_LIST_MAX];
} DoubleList;

// Config sections, one bit each
// A field's scope is the OR of the sections it may appear in: a key is rejected in any other
// section, and --help groups fields by scope
typedef enum {
  SECTION_COMMON   = 1 << 0,  // [common]
  SECTION_RUN      = 1 << 1,  // [[run]]
  SECTION_DEFAULTS = 1 << 2,  // [defaults.<algo>]
} Section;

// The scopes most rows have
enum {
  SCOPE_GLOBAL = SECTION_COMMON | SECTION_RUN,    // a [[run]] value overrides [common]
  SCOPE_ALGO   = SECTION_RUN | SECTION_DEFAULTS,  // every algorithm field
};

// One config key. Its value is parsed by `type` and written at `base + offset`, where `base` is
// &g_cfg for the runner's table and cfg_base() for an algorithm's
typedef struct FieldSpec {
  const char* name;
  FieldType   type;
  unsigned    scope;  // OR of the Sections the key may appear in
  size_t      offset;
  const void* lookup;           // the EnumEntry table of a FIELD_ENUM, NULL otherwise
  const char* default_display;  // the default as the user would write it; parsed like any value
  const char* description;      // for --help
} FieldSpec;

// Row constructors. A key takes the name of the struct member it's stored in, so the two can't
// disagree:
//   FIELD(PsoConfig, swarm_size, INT, "30", "Swarm size")
// FIELD and ENUM_FIELD are for an algorithm's table; FIELD_ROW spells out every column, for the
// runner's table, whose rows differ in scope
#define FIELD_ROW(cfg_type, member, kind, scope, lookup, default_display, description) \
  {#member, FIELD_##kind, scope, offsetof(cfg_type, member), lookup, default_display, description}
#define FIELD(cfg_type, member, kind, default_display, description) \
  FIELD_ROW(cfg_type, member, kind, SCOPE_ALGO, NULL, default_display, description)
#define ENUM_FIELD(cfg_type, member, lookup, default_display, description) \
  FIELD_ROW(cfg_type, member, ENUM, SCOPE_ALGO, lookup, default_display, description)
#define FIELDS_END {NULL, 0, 0, 0, NULL, NULL, NULL}

// One choice of a FIELD_ENUM: the name the user writes, and the enum value stored for it
// A table of them ends with {NULL, 0}
typedef struct {
  const char* name;
  int         value;
} EnumEntry;

// The name `table` maps to `value`, or "?" if none does
const char* enum_name(const EnumEntry* table, int value);

// --- Bounds ---

// What happens to a coordinate that leaves the domain, for algorithms that move points
//   clamp:   pinned to the bound it crossed
//   reflect: mirrored back inside, bouncing as many times as needed
//   random:  redrawn uniformly in the domain
// An infinite end of the domain can't be crossed, so the mode only matters at a finite one
typedef enum { BOUNDS_CLAMP, BOUNDS_REFLECT, BOUNDS_RANDOM } BoundsMode;

extern const EnumEntry bounds_table[];

// Brings one coordinate back into the domain; does nothing when it's already inside
// `v` is its velocity component, or NULL: clamp and random zero it, reflect flips its sign on an
// odd number of bounces
// A non-finite coordinate is always brought back to a finite value, even in an unbounded domain
void apply_bounds(double* x, double* v, BoundsMode mode);

// Dies when `mode` can't work on the run's domain: random with exactly one infinite end
// For each algorithm's validate
void check_bounds_mode(BoundsMode mode);

// --- The algorithm itself ---

// An algorithm is one file: src/algorithms/<name>.c defining `const Algorithm <name>_algorithm`
// is compiled and registered by the build
typedef struct Algorithm {
  const char*      name;
  const FieldSpec* fields;
  void* (*cfg_base)(void);  // the address of the algorithm's _Thread_local cfg

  // sizeof that cfg. It's copied with memcpy, so it must hold its data by value (DoubleList is the
  // pattern); pointers to constants and functions are fine
  size_t cfg_size;

  // Called once per run, after every field is set. Dies on an invalid config, and may resolve
  // derived values (e.g. pso's w_end = w) so dump and run see the final config
  void (*validate)(void);

  // Fills out->fitness, out->pos and out->stop. Prints only under --interactive
  void (*run)(RunResult* out);
} Algorithm;
