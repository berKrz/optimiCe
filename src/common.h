#pragma once

#include <stdint.h>

typedef struct Algorithm Algorithm;  // defined in algorithms.h
typedef struct FieldSpec FieldSpec;  // defined in algorithms.h

typedef enum { MINIMIZE, MAXIMIZE } OptDirection;

// Common config shared across all algorithms. Each algorithm's own parameters
// live in a file-scope `_Thread_local cfg` inside its own translation unit --
// private, out of the shared header and per-thread
typedef struct {
  uint32_t     seed;
  OptDirection direction;
  int          n_dims;
  double       domain_min;
  double       domain_max;
  int          interactive;

  double (*fitness_fn)(const double* pos);

  const Algorithm* algorithm;
} Config;

extern _Thread_local Config g_cfg;

#define LINE_WIDTH 50

// utils.c
void die(const char* msg);
void die_at(const char* source, const char* key, const char* msg);
void clear_screen(void);
void wait_for_enter(void);
void print_separator(void);
void print_iteration_header(int iter);
void print_iteration_line(int iter, double best_fitness);
void print_result(const double* best_pos, double best_fitness);

// rng.c
void   rng_init_with(uint32_t seed);
void   rng_init_auto(void);
double rng_next_double(void);
double rng_next_double_range(double lo, double hi);
int    rng_next_int(int lo, int hi);  // half-open: [lo, hi)
int    rng_next_bit(void);

// config.c
void apply_field(const char* source, const char* key, const char* val);
void validate(void);
void dump_config_fn(void);

// ini.c
void parse_ini(const char* filename);

// args.c
void parse_args(int argc, char** argv);

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
