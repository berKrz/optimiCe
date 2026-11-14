#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "algorithm.h"

// --- Config ---

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
    FIELD(PsoConfig, swarm_size, INT, "30", "Swarm size"),
    FIELD(PsoConfig, w, DOUBLE, "1.0", "Inertia weight (at the first iteration)"),
    FIELD(PsoConfig, w_end, DOUBLE, "nan",
          "Inertia at the last iteration, linear decay from w (unset = w)"),
    FIELD(PsoConfig, v_max, DOUBLE, "0.0", "Velocity cap, fraction of domain width (0 = off)"),
    FIELD(PsoConfig, c1, DOUBLE, "2.0", "Cognitive coefficient"),
    FIELD(PsoConfig, c2, DOUBLE, "2.0", "Social coefficient"),
    ENUM_FIELD(PsoConfig, pbest_init, pbest_init_table, "position", "pbest init"),
    ENUM_FIELD(PsoConfig, gbest_update, gbest_update_table, "async", "gbest update"),
    ENUM_FIELD(PsoConfig, bounds, bounds_table, "clamp",
               "Out-of-domain handling (ignored when the domain has no finite end)"),
    FIELDS_END,
};

static void* pso_cfg_base(void) { return &cfg; }

// --- Validate ---

static void pso_validate(void) {
  if (cfg.swarm_size <= 0) die_with_hint("swarm_size must be positive");
  if (cfg.w < 0.0) die_with_hint("w must be non-negative");
  // Not a check: an unset w_end means constant inertia, resolved here so dump and run see it
  if (isnan(cfg.w_end)) cfg.w_end = cfg.w;
  if (cfg.w_end < 0.0) die_with_hint("w_end must be non-negative");
  if (cfg.v_max < 0.0) die_with_hint("v_max must be non-negative (0 disables the cap)");
  if (cfg.c1 < 0.0) die_with_hint("c1 must be non-negative");
  if (cfg.c2 < 0.0) die_with_hint("c2 must be non-negative");
  check_bounds_mode(cfg.bounds);
  // pos, vel and pbest of every particle share one buffer
  check_fits_int(cfg.swarm_size, 3LL * g_cfg.n_dims, "swarm_size x n_dims");
}

// --- State ---

// All particles share one flat buffer, carved per-particle contiguous:
//   [p0.pos | p0.vel | p0.pbest | p1.pos | p1.vel | p1.pbest | ...]
// Each slot is n_dims doubles. The pointers in Particle index into it
// Access is swarm->particles[i].pos[d]
typedef struct {
  double* pos;
  double* vel;
  double* pbest;
  double  fitness;
  double  pbest_fitness;
  int     pbest_updated;
} Particle;

typedef struct {
  Particle* particles;  // swarm_size
  double*   data;       // the buffer the particles point into
  double*   gbest_pos;  // n_dims; always some particle's pbest
  double    gbest_fitness;
  double    v_cap;  // largest velocity component allowed; 0 when there's no cap
} Swarm;

static Swarm swarm_alloc(void) {
  size_t n = (size_t) cfg.swarm_size, dims = (size_t) g_cfg.n_dims;
  return (Swarm) {
      .particles = xmalloc(n * sizeof(Particle)),
      .data      = xmalloc(n * 3 * dims * sizeof(double)),
      .gbest_pos = xmalloc(dims * sizeof(double)),
  };
}

static void swarm_free(Swarm* swarm) {
  free(swarm->gbest_pos);
  free(swarm->data);
  free(swarm->particles);
}

// --- Steps ---

static void copy_pos(double* dst, const double* src) {
  memcpy(dst, src, (size_t) g_cfg.n_dims * sizeof(double));
}

static void update_gbest(Swarm* swarm, const Particle* p) {
  if (is_better(p->pbest_fitness, swarm->gbest_fitness)) {
    swarm->gbest_fitness = p->pbest_fitness;
    copy_pos(swarm->gbest_pos, p->pbest);
  }
}

// v_max is a fraction of the domain's width; an unbounded domain has none, so the init range's
// stands in as the scale of the search
static double velocity_cap(void) {
  int    bounded = isfinite(g_cfg.domain_min) && isfinite(g_cfg.domain_max);
  double width   = bounded ? g_cfg.domain_max - g_cfg.domain_min : g_cfg.init_max - g_cfg.init_min;
  return cfg.v_max * width;
}

static void init_swarm(Swarm* swarm) {
  swarm->v_cap = velocity_cap();

  for (int i = 0; i < cfg.swarm_size; i++) {
    Particle* p      = &swarm->particles[i];
    double*   slot   = swarm->data + i * 3 * g_cfg.n_dims;
    p->pos           = slot;
    p->vel           = slot + g_cfg.n_dims;
    p->pbest         = slot + 2 * g_cfg.n_dims;
    p->pbest_updated = 0;

    for (int d = 0; d < g_cfg.n_dims; d++) {
      p->pos[d] = rng_next_double_range(g_cfg.init_min, g_cfg.init_max);
      p->vel[d] = 0.0;
    }

    p->fitness = evaluate(p->pos);

    if (cfg.pbest_init == PBEST_POSITION) {
      copy_pos(p->pbest, p->pos);
      p->pbest_fitness = p->fitness;
    } else {
      for (int d = 0; d < g_cfg.n_dims; d++)
        p->pbest[d] = rng_next_double_range(g_cfg.init_min, g_cfg.init_max);
      p->pbest_fitness = evaluate(p->pbest);
    }
  }

  // The first particle seeds gbest, so gbest is always some particle's pbest, never a placeholder
  swarm->gbest_fitness = swarm->particles[0].pbest_fitness;
  copy_pos(swarm->gbest_pos, swarm->particles[0].pbest);
  for (int i = 1; i < cfg.swarm_size; i++) update_gbest(swarm, &swarm->particles[i]);
}

// Inertia for iteration `iter` (0-based): linear from w at the first iteration to w_end at the
// last
// With w_end == w the difference is exactly 0, so constant inertia stays bit-identical to w
static double inertia(int iter) {
  double frac = g_cfg.iterations > 1 ? (double) iter / (g_cfg.iterations - 1) : 0.0;
  return cfg.w + (cfg.w_end - cfg.w) * frac;
}

static void update_swarm(Swarm* swarm, double w) {
  for (int i = 0; i < cfg.swarm_size; i++) {
    Particle* p      = &swarm->particles[i];
    p->pbest_updated = 0;

    for (int d = 0; d < g_cfg.n_dims; d++) {
      double r1 = rng_next_double();
      double r2 = rng_next_double();

      p->vel[d] = w * p->vel[d] + cfg.c1 * r1 * (p->pbest[d] - p->pos[d]) +
                  cfg.c2 * r2 * (swarm->gbest_pos[d] - p->pos[d]);

      if (swarm->v_cap > 0.0) {
        if (p->vel[d] > swarm->v_cap) p->vel[d] = swarm->v_cap;
        else if (p->vel[d] < -swarm->v_cap) p->vel[d] = -swarm->v_cap;
      }

      p->pos[d] += p->vel[d];
      apply_bounds(&p->pos[d], &p->vel[d], cfg.bounds);
    }

    p->fitness = evaluate(p->pos);

    if (is_better(p->fitness, p->pbest_fitness)) {
      p->pbest_fitness = p->fitness;
      copy_pos(p->pbest, p->pos);
      p->pbest_updated = 1;
    }

    // Async: gbest moves as soon as any particle improves it, so later particles in the same
    // iteration already steer toward the new best
    if (cfg.gbest_update == GBEST_ASYNC) update_gbest(swarm, p);
  }

  // Sync: every particle moved against the gbest from the start of the iteration; refresh it once
  if (cfg.gbest_update == GBEST_SYNC)
    for (int i = 0; i < cfg.swarm_size; i++) update_gbest(swarm, &swarm->particles[i]);
}

// --- Interactive output ---

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

// The whole swarm, then a pause
static void show_swarm(const Swarm* swarm) {
  for (int i = 0; i < cfg.swarm_size; i++)
    print_particle(&swarm->particles[i], i, swarm->gbest_fitness);
  printf("gbest = %g\n", swarm->gbest_fitness);
  wait_for_enter();
  print_separator();
}

// --- Entry point ---

static void pso_run(RunResult* out) {
  Swarm swarm = swarm_alloc();
  init_swarm(&swarm);

  if (g_opts.interactive) {
    printf("Initial Swarm\n\n");
    show_swarm(&swarm);
  }

  StopState stop = stop_init();
  for (int i = 0; i < g_cfg.iterations; i++) {
    double w = inertia(i);
    update_swarm(&swarm, w);

    if (g_opts.interactive) {
      print_iteration_header(i);
      if (cfg.w_end != cfg.w) printf("w = %g\n\n", w);
      show_swarm(&swarm);
    }
    if (end_iteration(&stop, swarm.gbest_fitness)) break;
  }

  copy_pos(out->pos, swarm.gbest_pos);
  out->fitness = swarm.gbest_fitness;
  out->stop    = stop;

  swarm_free(&swarm);
}

const Algorithm pso_algorithm = {
    .name     = "pso",
    .fields   = pso_fields,
    .cfg_base = pso_cfg_base,
    .cfg_size = sizeof(PsoConfig),
    .validate = pso_validate,
    .run      = pso_run,
};
