#pragma once

#include <stdint.h>

typedef struct Algorithm Algorithm;  // defined in algorithms.h
typedef struct FieldSpec FieldSpec;  // defined in algorithms.h

typedef enum { MINIMIZE, MAXIMIZE } OptDirection;
typedef enum { ALGO_GA, ALGO_PSO } AlgorithmKind;
typedef enum { PBEST_POSITION, PBEST_RANDOM } PbestInit;

typedef struct {
  int _placeholder;  // replaced wholesale when GA is ported
} GaConfig;

typedef struct {
  int       swarm_size;
  int       iterations;
  double    w;
  double    c1;
  double    c2;
  PbestInit pbest_init;
} PsoConfig;

typedef struct {
  uint64_t     seed;
  OptDirection direction;
  int          n_dims;
  double       domain_min;
  double       domain_max;
  int          interactive;

  double (*fitness_fn)(const double* pos);

  const Algorithm* algorithm;

  union {
    GaConfig  ga;
    PsoConfig pso;
  };
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

// rng.c
void   rng_init_with(uint64_t seed);
void   rng_init_auto(void);
double rng_next_double(void);
double rng_next_double_range(double lo, double hi);
int    rng_next_int(int lo, int hi);
int    rng_next_bit(void);

// config.c
void config_set_common_defaults(void);
void apply_field(const char* source, const char* key, const char* val);
void apply_field(const char* source, const char* key, const char* val);
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
} FitnessEntry;

extern const FitnessEntry fitness_table[];
