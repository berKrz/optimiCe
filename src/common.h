#pragma once

#include <stdint.h>

typedef enum { MINIMIZE, MAXIMIZE } OptDirection;
typedef enum { ALGO_GA, ALGO_PSO } AlgorithmKind;
typedef enum { PBEST_POSITION, PBEST_RANDOM } PbestInit;

typedef struct {
  int _placeholder;
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
  /* Common fields - valid regardless of which algorithm is selected */
  AlgorithmKind kind;
  uint64_t      seed;
  OptDirection  direction;
  int           n_dims;
  double        domain_min;
  double        domain_max;
  int           interactive;

  /* Shared fitness - signature is uniform across algorithms */
  double (*fitness_fn)(const double *pos, int n_dims);

  /* Algorithm-specific arm - selected by `kind` */
  union {
    GaConfig  ga;
    PsoConfig pso;
  };
} Config;

extern _Thread_local Config g_cfg;
