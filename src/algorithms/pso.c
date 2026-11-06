#include <float.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#include "algorithms.h"
#include "common.h"

// --- PSO-private config state ---

typedef enum { PBEST_POSITION, PBEST_RANDOM } PbestInit;

typedef struct {
  int       swarm_size;
  int       iterations;
  double    w;
  double    c1;
  double    c2;
  PbestInit pbest_init;
} PsoConfig;

static _Thread_local PsoConfig cfg;

// --- Field table ---

static const EnumEntry pbest_init_table[] = {
    {"position", PBEST_POSITION},
    {"random",   PBEST_RANDOM  },
    {NULL,       0             },
};

const FieldSpec pso_fields[] = {
    {"swarm_size", FIELD_INT, offsetof(PsoConfig, swarm_size), NULL, "30", "Swarm size"},
    {"iterations", FIELD_INT, offsetof(PsoConfig, iterations), NULL, "100", "Number of iterations"},
    {"w", FIELD_DOUBLE, offsetof(PsoConfig, w), NULL, "1.0", "Inertia weight"},
    {"c1", FIELD_DOUBLE, offsetof(PsoConfig, c1), NULL, "2.0", "Cognitive coefficient"},
    {"c2", FIELD_DOUBLE, offsetof(PsoConfig, c2), NULL, "2.0", "Social coefficient"},
    {"pbest_init", FIELD_ENUM, offsetof(PsoConfig, pbest_init), pbest_init_table, "position",
     "pbest init: position | random"},
    {NULL, 0, 0, NULL, NULL, NULL},
};

void* pso_cfg_base(void) { return &cfg; }

// --- Validation ---

void pso_validate(void) {
  if (cfg.swarm_size <= 0) die("pso: swarm_size must be positive");
  if (cfg.iterations <= 0) die("pso: iterations must be positive");
  if (cfg.w < 0.0) die("pso: w must be non-negative");
  if (cfg.c1 < 0.0) die("pso: c1 must be non-negative");
  if (cfg.c2 < 0.0) die("pso: c2 must be non-negative");
}

// --- Dump ---

void pso_dump(void) {
  printf("swarm_size = %d\n", cfg.swarm_size);
  printf("iterations = %d\n", cfg.iterations);
  printf("w          = %g\n", cfg.w);
  printf("c1         = %g\n", cfg.c1);
  printf("c2         = %g\n", cfg.c2);
  printf("pbest_init = %s\n", cfg.pbest_init == PBEST_POSITION ? "position" : "random");
}

// --- Particle layout ---
//
// All particles share one flat buffer, carved per-particle contiguous:
//   [p0.pos | p0.vel | p0.pbest | p1.pos | p1.vel | p1.pbest | ...]
// Each slot is n_dims doubles. The pointers in Particle index into it
// Access is swarm[i].pos[d]

typedef struct {
  double* pos;
  double* vel;
  double* pbest;
  double  fitness;
  double  pbest_fitness;
  int     pbest_updated;
} Particle;

// --- Internals ---

static void copy_pos(double* dst, const double* src) {
  for (int d = 0; d < g_cfg.n_dims; d++) dst[d] = src[d];
}

static void init_swarm(Particle* swarm, double* data, double* gbest_pos, double* gbest_fitness) {
  int n            = g_cfg.n_dims;
  int per_particle = 3 * n;

  *gbest_fitness = g_cfg.direction == MINIMIZE ? DBL_MAX : -DBL_MAX;

  for (int i = 0; i < cfg.swarm_size; i++) {
    double* slot           = data + i * per_particle;
    swarm[i].pos           = slot;
    swarm[i].vel           = slot + n;
    swarm[i].pbest         = slot + 2 * n;
    swarm[i].pbest_updated = 0;

    for (int d = 0; d < n; d++) {
      swarm[i].pos[d] = rng_next_double_range(g_cfg.domain_min, g_cfg.domain_max);
      swarm[i].vel[d] = 0.0;
    }

    swarm[i].fitness = g_cfg.fitness_fn(swarm[i].pos);

    if (cfg.pbest_init == PBEST_POSITION) {
      copy_pos(swarm[i].pbest, swarm[i].pos);
      swarm[i].pbest_fitness = swarm[i].fitness;
    } else {
      for (int d = 0; d < n; d++)
        swarm[i].pbest[d] = rng_next_double_range(g_cfg.domain_min, g_cfg.domain_max);
      swarm[i].pbest_fitness = g_cfg.fitness_fn(swarm[i].pbest);
    }

    if (is_better(swarm[i].pbest_fitness, *gbest_fitness)) {
      *gbest_fitness = swarm[i].pbest_fitness;
      copy_pos(gbest_pos, swarm[i].pbest);
    }
  }
}

static void update_swarm(Particle* swarm, double* gbest_pos, double* gbest_fitness) {
  for (int i = 0; i < cfg.swarm_size; i++) {
    swarm[i].pbest_updated = 0;

    for (int d = 0; d < g_cfg.n_dims; d++) {
      double r1 = rng_next_double();
      double r2 = rng_next_double();

      swarm[i].vel[d] = cfg.w * swarm[i].vel[d] +
                        cfg.c1 * r1 * (swarm[i].pbest[d] - swarm[i].pos[d]) +
                        cfg.c2 * r2 * (gbest_pos[d] - swarm[i].pos[d]);

      swarm[i].pos[d] += swarm[i].vel[d];

      if (swarm[i].pos[d] < g_cfg.domain_min) {
        swarm[i].pos[d] = g_cfg.domain_min;
        swarm[i].vel[d] = 0.0;
      } else if (swarm[i].pos[d] > g_cfg.domain_max) {
        swarm[i].pos[d] = g_cfg.domain_max;
        swarm[i].vel[d] = 0.0;
      }
    }

    swarm[i].fitness = g_cfg.fitness_fn(swarm[i].pos);

    if (is_better(swarm[i].fitness, swarm[i].pbest_fitness)) {
      swarm[i].pbest_fitness = swarm[i].fitness;
      copy_pos(swarm[i].pbest, swarm[i].pos);
      swarm[i].pbest_updated = 1;
    }

    // Async gbest update: gbest moves as soon as any particle improves it,
    // within the same iteration - later particles see the updated gbest.
    if (is_better(swarm[i].pbest_fitness, *gbest_fitness)) {
      *gbest_fitness = swarm[i].pbest_fitness;
      copy_pos(gbest_pos, swarm[i].pbest);
    }
  }
}

// --- Presentation ---

static void print_particle(const Particle* p, int index, double gbest_fitness) {
  int is_gbest_holder = (p->pbest_fitness == gbest_fitness);

  if (is_gbest_holder) printf(">> Particle %2d", index);
  else printf("   Particle %2d", index);

  if (p->pbest_updated) printf("   (*)\n");
  else printf("\n");

  printf("     pos: ");
  print_vec(p->pos);
  printf("\n");

  printf("     vel: ");
  print_vec(p->vel);
  printf("\n");

  printf("     fitness = %g   pbest = %g\n\n", p->fitness, p->pbest_fitness);
}

static void print_swarm(const Particle* swarm, double gbest_fitness) {
  for (int i = 0; i < cfg.swarm_size; i++) print_particle(&swarm[i], i, gbest_fitness);
  printf("gbest = %g\n", gbest_fitness);
}

// --- Entry point ---

void pso_run(void) {
  int n            = g_cfg.n_dims;
  int swarm_size   = cfg.swarm_size;
  int per_particle = 3 * n;

  double*   data      = malloc(swarm_size * per_particle * sizeof(double));
  Particle* swarm     = malloc(swarm_size * sizeof(Particle));
  double*   gbest_pos = malloc(n * sizeof(double));
  double    gbest_fitness;

  if (!data || !swarm || !gbest_pos) die("memory allocation failed");

  init_swarm(swarm, data, gbest_pos, &gbest_fitness);

  if (g_cfg.interactive) {
    printf("Initial Swarm\n\n");
    print_swarm(swarm, gbest_fitness);
    wait_for_enter();
    print_separator();
  }

  for (int i = 0; i < cfg.iterations; i++) {
    update_swarm(swarm, gbest_pos, &gbest_fitness);

    if (g_cfg.interactive) {
      print_iteration_header(i);
      print_swarm(swarm, gbest_fitness);
      wait_for_enter();
      print_separator();
    } else {
      print_iteration_line(i, gbest_fitness);
    }
  }

  print_result(gbest_pos, gbest_fitness);

  free(gbest_pos);
  free(swarm);
  free(data);
}
