#include <errno.h>
#include <float.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "algorithms.h"
#include "common.h"

// --- Field handlers ---

static void apply_swarm_size(const char* source, const char* key, const char* val) {
  char* end;
  errno  = 0;
  long v = strtol(val, &end, 10);
  if (*val == '\0' || *end != '\0' || errno == ERANGE || v <= 0 || v > INT_MAX)
    die_at(source, key, "deve ser um inteiro positivo");
  g_cfg.pso.swarm_size = (int) v;
}

static void apply_iterations(const char* source, const char* key, const char* val) {
  char* end;
  errno  = 0;
  long v = strtol(val, &end, 10);
  if (*val == '\0' || *end != '\0' || errno == ERANGE || v <= 0 || v > INT_MAX)
    die_at(source, key, "deve ser um inteiro positivo");
  g_cfg.pso.iterations = (int) v;
}

static void apply_w(const char* source, const char* key, const char* val) {
  char* end;
  errno    = 0;
  double v = strtod(val, &end);
  if (*val == '\0' || *end != '\0' || errno == ERANGE || v < 0.0)
    die_at(source, key, "deve ser um número não-negativo");
  g_cfg.pso.w = v;
}

static void apply_c1(const char* source, const char* key, const char* val) {
  char* end;
  errno    = 0;
  double v = strtod(val, &end);
  if (*val == '\0' || *end != '\0' || errno == ERANGE || v < 0.0)
    die_at(source, key, "deve ser um número não-negativo");
  g_cfg.pso.c1 = v;
}

static void apply_c2(const char* source, const char* key, const char* val) {
  char* end;
  errno    = 0;
  double v = strtod(val, &end);
  if (*val == '\0' || *end != '\0' || errno == ERANGE || v < 0.0)
    die_at(source, key, "deve ser um número não-negativo");
  g_cfg.pso.c2 = v;
}

static void apply_pbest_init(const char* source, const char* key, const char* val) {
  if (strcmp(val, "position") == 0) g_cfg.pso.pbest_init = PBEST_POSITION;
  else if (strcmp(val, "random") == 0) g_cfg.pso.pbest_init = PBEST_RANDOM;
  else die_at(source, key, "deve ser 'position' ou 'random'");
}

// --- Field table ---

const FieldSpec pso_fields[] = {
    {"swarm_size", FIELD_INT,    "30",       "Tamanho do enxame",                  apply_swarm_size},
    {"iterations", FIELD_INT,    "100",      "Número de iterações",                apply_iterations},
    {"w",          FIELD_DOUBLE, "1.0",      "Peso de inércia",                    apply_w         },
    {"c1",         FIELD_DOUBLE, "2.0",      "Coeficiente cognitivo",              apply_c1        },
    {"c2",         FIELD_DOUBLE, "2.0",      "Coeficiente social",                 apply_c2        },
    {"pbest_init", FIELD_ENUM,   "position", "Início do pbest: position | random", apply_pbest_init},
    {NULL,         0,            NULL,       NULL,                                 NULL            },
};

// --- Dump ---

void pso_dump(void) {
  printf("swarm_size = %d\n", g_cfg.pso.swarm_size);
  printf("iterations = %d\n", g_cfg.pso.iterations);
  printf("w          = %g\n", g_cfg.pso.w);
  printf("c1         = %g\n", g_cfg.pso.c1);
  printf("c2         = %g\n", g_cfg.pso.c2);
  printf("pbest_init = %s\n", g_cfg.pso.pbest_init == PBEST_POSITION ? "position" : "random");
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

static int is_better(double a, double b) { return g_cfg.direction == MINIMIZE ? a < b : a > b; }

static void copy_pos(double* dst, const double* src) {
  for (int d = 0; d < g_cfg.n_dims; d++) dst[d] = src[d];
}

static void init_swarm(Particle* swarm, double* data, double* gbest_pos, double* gbest_fitness) {
  int n            = g_cfg.n_dims;
  int per_particle = 3 * n;

  *gbest_fitness = g_cfg.direction == MINIMIZE ? DBL_MAX : -DBL_MAX;

  for (int i = 0; i < g_cfg.pso.swarm_size; i++) {
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

    if (g_cfg.pso.pbest_init == PBEST_POSITION) {
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
  for (int i = 0; i < g_cfg.pso.swarm_size; i++) {
    swarm[i].pbest_updated = 0;

    for (int d = 0; d < g_cfg.n_dims; d++) {
      double r1 = rng_next_double();
      double r2 = rng_next_double();

      swarm[i].vel[d] = g_cfg.pso.w * swarm[i].vel[d] +
                        g_cfg.pso.c1 * r1 * (swarm[i].pbest[d] - swarm[i].pos[d]) +
                        g_cfg.pso.c2 * r2 * (gbest_pos[d] - swarm[i].pos[d]);

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

static void print_vec(const double* v) {
  printf("[ ");
  for (int d = 0; d < g_cfg.n_dims; d++) printf("%7.3f ", v[d]);
  printf("]");
}

static void print_particle(const Particle* p, int index, double gbest_fitness) {
  int is_gbest_holder = (p->pbest_fitness == gbest_fitness);

  if (is_gbest_holder) printf(">> Partícula %2d", index);
  else printf("   Partícula %2d", index);

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
  for (int i = 0; i < g_cfg.pso.swarm_size; i++) print_particle(&swarm[i], i, gbest_fitness);
  printf("gbest = %g\n", gbest_fitness);
}

// --- Entry point ---

void pso_run(void) {
  int n            = g_cfg.n_dims;
  int swarm_size   = g_cfg.pso.swarm_size;
  int per_particle = 3 * n;

  double*   data      = malloc(swarm_size * per_particle * sizeof(double));
  Particle* swarm     = malloc(swarm_size * sizeof(Particle));
  double*   gbest_pos = malloc(n * sizeof(double));
  double    gbest_fitness;

  if (!data || !swarm || !gbest_pos) die("falha na alocação de memória");

  init_swarm(swarm, data, gbest_pos, &gbest_fitness);

  if (g_cfg.interactive) {
    printf("Enxame Inicial\n\n");
    print_swarm(swarm, gbest_fitness);
    wait_for_enter();
    print_separator();
  }

  for (int i = 0; i < g_cfg.pso.iterations; i++) {
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
