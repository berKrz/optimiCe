#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#include "algorithms.h"
#include "common.h"

// --- PSO-private config state ---

typedef enum { PBEST_POSITION, PBEST_RANDOM } PbestInit;
typedef enum { GBEST_ASYNC, GBEST_SYNC } GbestUpdate;

typedef struct {
  int    swarm_size;
  double w;
  double w_end;  // inertia at the last iteration; NaN until validate resolves it to w
  double v_max;  // velocity cap as a fraction of the domain width (of the init range's when
                 // the domain is unbounded); 0 disables it
  double      c1;
  double      c2;
  PbestInit   pbest_init;
  GbestUpdate gbest_update;
  BoundsMode  bounds;
} PsoConfig;

static _Thread_local PsoConfig cfg;

// --- Field table ---

static const EnumEntry pbest_init_table[] = {
    {"position", PBEST_POSITION},
    {"random",   PBEST_RANDOM  },
    {NULL,       0             },
};

static const EnumEntry gbest_update_table[] = {
    {"async", GBEST_ASYNC},
    {"sync",  GBEST_SYNC },
    {NULL,    0          },
};

static const FieldSpec pso_fields[] = {
    {"swarm_size", FIELD_INT, SCOPE_ALGO, offsetof(PsoConfig, swarm_size), NULL, "30",
     "Swarm size"},
    {"w", FIELD_DOUBLE, SCOPE_ALGO, offsetof(PsoConfig, w), NULL, "1.0",
     "Inertia weight (at the first iteration)"},
    {"w_end", FIELD_DOUBLE, SCOPE_ALGO, offsetof(PsoConfig, w_end), NULL, "nan",
     "Inertia at the last iteration, linear decay from w (unset = w)"},
    {"v_max", FIELD_DOUBLE, SCOPE_ALGO, offsetof(PsoConfig, v_max), NULL, "0.0",
     "Velocity cap, fraction of domain width (0 = off)"},
    {"c1", FIELD_DOUBLE, SCOPE_ALGO, offsetof(PsoConfig, c1), NULL, "2.0", "Cognitive coefficient"},
    {"c2", FIELD_DOUBLE, SCOPE_ALGO, offsetof(PsoConfig, c2), NULL, "2.0", "Social coefficient"},
    {"pbest_init", FIELD_ENUM, SCOPE_ALGO, offsetof(PsoConfig, pbest_init), pbest_init_table,
     "position", "pbest init: position | random"},
    {"gbest_update", FIELD_ENUM, SCOPE_ALGO, offsetof(PsoConfig, gbest_update), gbest_update_table,
     "async", "gbest update: async | sync"},
    {"bounds", FIELD_ENUM, SCOPE_ALGO, offsetof(PsoConfig, bounds), bounds_table, "clamp",
     "Out-of-domain handling: clamp | reflect | random (ignored when the domain has no finite "
     "end)"},
    {NULL, 0, 0, 0, NULL, NULL, NULL},
};

static void* pso_cfg_base(void) { return &cfg; }

// --- Validation ---

static void pso_validate(void) {
  if (cfg.swarm_size <= 0) die("swarm_size must be positive");
  if (cfg.w < 0.0) die("w must be non-negative");
  // Not a check: an unset w_end means constant inertia, resolved here so dump and run see it
  if (isnan(cfg.w_end)) cfg.w_end = cfg.w;
  if (cfg.w_end < 0.0) die("w_end must be non-negative");
  if (cfg.v_max < 0.0) die("v_max must be non-negative (0 disables the cap)");
  if (cfg.c1 < 0.0) die("c1 must be non-negative");
  if (cfg.c2 < 0.0) die("c2 must be non-negative");
  check_bounds_mode(cfg.bounds);
  // pos, vel and pbest of every particle share one buffer
  check_fits_int(cfg.swarm_size, 3LL * g_cfg.n_dims, "swarm_size x n_dims");
}

// --- Dump ---

static void pso_dump(void) {
  printf("swarm_size   = %d\n", cfg.swarm_size);
  printf("w            = %g\n", cfg.w);
  printf("w_end        = %g\n", cfg.w_end);
  printf("v_max        = %g\n", cfg.v_max);
  printf("c1           = %g\n", cfg.c1);
  printf("c2           = %g\n", cfg.c2);
  printf("pbest_init   = %s\n", enum_name(pbest_init_table, cfg.pbest_init));
  printf("gbest_update = %s\n", enum_name(gbest_update_table, cfg.gbest_update));
  printf("bounds       = %s\n", enum_name(bounds_table, cfg.bounds));
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

static void update_gbest(const Particle* p, double* gbest_pos, double* gbest_fitness) {
  if (is_better(p->pbest_fitness, *gbest_fitness)) {
    *gbest_fitness = p->pbest_fitness;
    copy_pos(gbest_pos, p->pbest);
  }
}

static void init_swarm(Particle* swarm, double* data, double* gbest_pos, double* gbest_fitness) {
  int per_particle = 3 * g_cfg.n_dims;

  for (int i = 0; i < cfg.swarm_size; i++) {
    double* slot           = data + i * per_particle;
    swarm[i].pos           = slot;
    swarm[i].vel           = slot + g_cfg.n_dims;
    swarm[i].pbest         = slot + 2 * g_cfg.n_dims;
    swarm[i].pbest_updated = 0;

    for (int d = 0; d < g_cfg.n_dims; d++) {
      swarm[i].pos[d] = rng_next_double_range(g_cfg.init_min, g_cfg.init_max);
      swarm[i].vel[d] = 0.0;
    }

    swarm[i].fitness = evaluate(swarm[i].pos);

    if (cfg.pbest_init == PBEST_POSITION) {
      copy_pos(swarm[i].pbest, swarm[i].pos);
      swarm[i].pbest_fitness = swarm[i].fitness;
    } else {
      for (int d = 0; d < g_cfg.n_dims; d++)
        swarm[i].pbest[d] = rng_next_double_range(g_cfg.init_min, g_cfg.init_max);
      swarm[i].pbest_fitness = evaluate(swarm[i].pbest);
    }
  }

  // The first particle seeds gbest, so gbest is always some particle's pbest, never a placeholder
  *gbest_fitness = swarm[0].pbest_fitness;
  copy_pos(gbest_pos, swarm[0].pbest);
  for (int i = 1; i < cfg.swarm_size; i++) update_gbest(&swarm[i], gbest_pos, gbest_fitness);
}

// Inertia for iteration `iter` (0-based): linear from w at the first iteration to w_end at the
// last
// With w_end == w the difference is exactly 0, so constant inertia stays bit-identical to w
static double inertia(int iter) {
  double frac = g_cfg.iterations > 1 ? (double) iter / (g_cfg.iterations - 1) : 0.0;
  return cfg.w + (cfg.w_end - cfg.w) * frac;
}

static void update_swarm(Particle* swarm, double* gbest_pos, double* gbest_fitness, double w) {
  // v_max is a fraction of the domain's width; an unbounded domain has none, so the init range's
  // stands in as the scale of the search
  int    bounded = isfinite(g_cfg.domain_min) && isfinite(g_cfg.domain_max);
  double width   = bounded ? g_cfg.domain_max - g_cfg.domain_min : g_cfg.init_max - g_cfg.init_min;
  double v_cap   = cfg.v_max * width;  // 0 when disabled

  for (int i = 0; i < cfg.swarm_size; i++) {
    swarm[i].pbest_updated = 0;

    for (int d = 0; d < g_cfg.n_dims; d++) {
      double r1 = rng_next_double();
      double r2 = rng_next_double();

      swarm[i].vel[d] = w * swarm[i].vel[d] + cfg.c1 * r1 * (swarm[i].pbest[d] - swarm[i].pos[d]) +
                        cfg.c2 * r2 * (gbest_pos[d] - swarm[i].pos[d]);

      if (v_cap > 0.0) {
        if (swarm[i].vel[d] > v_cap) swarm[i].vel[d] = v_cap;
        else if (swarm[i].vel[d] < -v_cap) swarm[i].vel[d] = -v_cap;
      }

      swarm[i].pos[d] += swarm[i].vel[d];
      apply_bounds(&swarm[i].pos[d], &swarm[i].vel[d], cfg.bounds);
    }

    swarm[i].fitness = evaluate(swarm[i].pos);

    if (is_better(swarm[i].fitness, swarm[i].pbest_fitness)) {
      swarm[i].pbest_fitness = swarm[i].fitness;
      copy_pos(swarm[i].pbest, swarm[i].pos);
      swarm[i].pbest_updated = 1;
    }

    // Async: gbest moves as soon as any particle improves it, so later particles in the same
    // iteration already steer toward the new best
    if (cfg.gbest_update == GBEST_ASYNC) update_gbest(&swarm[i], gbest_pos, gbest_fitness);
  }

  // Sync: every particle moved against the gbest from the start of the iteration; refresh it once
  if (cfg.gbest_update == GBEST_SYNC)
    for (int i = 0; i < cfg.swarm_size; i++) update_gbest(&swarm[i], gbest_pos, gbest_fitness);
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

static void pso_run(RunResult* out) {
  int per_particle = 3 * g_cfg.n_dims;

  double*   data      = xmalloc((size_t) cfg.swarm_size * per_particle * sizeof(double));
  Particle* swarm     = xmalloc((size_t) cfg.swarm_size * sizeof(Particle));
  double*   gbest_pos = xmalloc((size_t) g_cfg.n_dims * sizeof(double));
  double    gbest_fitness;

  init_swarm(swarm, data, gbest_pos, &gbest_fitness);

  if (g_opts.interactive) {
    printf("Initial Swarm\n\n");
    print_swarm(swarm, gbest_fitness);
    wait_for_enter();
    print_separator();
  }

  StopState stop = stop_init();
  for (int i = 0; i < g_cfg.iterations; i++) {
    double w = inertia(i);
    update_swarm(swarm, gbest_pos, &gbest_fitness, w);

    if (g_opts.interactive) {
      print_iteration_header(i);
      if (cfg.w_end != cfg.w) printf("w = %g\n\n", w);
      print_swarm(swarm, gbest_fitness);
      wait_for_enter();
      print_separator();
    } else {
      print_iteration_line(i, gbest_fitness);
    }
    if (stop_check(&stop, i, gbest_fitness)) break;
  }

  copy_pos(out->pos, gbest_pos);
  out->fitness = gbest_fitness;
  out->stop    = stop;

  free(gbest_pos);
  free(swarm);
  free(data);
}

const Algorithm pso_algorithm = {
    .name     = "pso",
    .fields   = pso_fields,
    .cfg_base = pso_cfg_base,
    .cfg_size = sizeof(PsoConfig),
    .validate = pso_validate,
    .dump     = pso_dump,
    .run      = pso_run,
};
