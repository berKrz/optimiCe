#include <criterion/criterion.h>
#include <criterion/new/assert.h>
#include <math.h>
#include <stdlib.h>

#include "helpers.h"

static void setup(void) {
  set_defaults(algo_named("pso")->fields, algo_named("pso")->cfg_base());
  g_cfg_rw.domain_min = -5.0;
  g_cfg_rw.domain_max = 5.0;
  g_cfg_rw.init_min   = -5.0;
  g_cfg_rw.init_max   = 5.0;
}

TestSuite(pso, .init = setup);

#define PARAM(key) (*(double*) live_field("pso", key))

// --- Inertia ---

// Progress at iteration `i` (0-based) of an 11-iteration run
static double at(int i) { return (i + 1.0) / 11.0; }

// Progress at its first iteration
static const double FIRST = 1.0 / 11.0;

// Linear from w at the first iteration to w_end at the last
Test(pso, inertia_schedule) {
  PARAM("w")     = 0.9;
  PARAM("w_end") = 0.4;
  cr_expect(eq(dbl, inertia(at(0), FIRST), 0.9));
  cr_expect(epsilon_eq(dbl, inertia(at(5), FIRST), 0.65, 1e-15));
  cr_expect(eq(dbl, inertia(at(10), FIRST), 0.4));
}

// Both ends exact, including pairs where w + (w_end - w) rounds away from w_end
Test(pso, inertia_endpoints_exact) {
  const double pairs[][2] = {
      {0.9, 0.4},
      {1.2, 0.1},
      {0.4, 0.9}
  };
  for (size_t k = 0; k < sizeof pairs / sizeof pairs[0]; k++) {
    PARAM("w")     = pairs[k][0];
    PARAM("w_end") = pairs[k][1];
    cr_expect(eq(dbl, inertia(at(0), FIRST), pairs[k][0]), "w = %g, w_end = %g", pairs[k][0],
              pairs[k][1]);
    cr_expect(eq(dbl, inertia(at(10), FIRST), pairs[k][1]), "w = %g, w_end = %g", pairs[k][0],
              pairs[k][1]);
  }
}

// A first iteration that is also the last: w, with no division by 1 - 1 = 0
Test(pso, inertia_single_iteration) {
  PARAM("w")     = 0.9;
  PARAM("w_end") = 0.4;
  cr_expect(eq(dbl, inertia(1.0, 1.0), 0.9));
}

// Measured from the first iteration's progress, however far setup got: under an evaluation budget
// setup's evaluations count, so the first iteration's progress is above 0
Test(pso, inertia_rescaled_from_first_progress) {
  PARAM("w")     = 0.9;
  PARAM("w_end") = 0.4;
  cr_expect(eq(dbl, inertia(0.1, 0.1), 0.9));
  cr_expect(epsilon_eq(dbl, inertia(0.55, 0.1), 0.65, 1e-15));
}

// Constant inertia stays bit-identical to w
Test(pso, inertia_constant) {
  PARAM("w")     = 0.7;
  PARAM("w_end") = 0.7;
  for (int i = 0; i < 11; i++) cr_expect(eq(dbl, inertia(at(i), FIRST), 0.7), "iteration %d", i);
}

// --- Velocity cap ---

Test(pso, velocity_cap_domain_width) {
  PARAM("v_max") = 0.2;
  cr_expect(eq(dbl, velocity_cap(), 2.0));
}

// An unbounded domain has no width: the init range's stands in
Test(pso, velocity_cap_unbounded_uses_init_range) {
  PARAM("v_max")      = 0.2;
  g_cfg_rw.domain_min = -INFINITY;
  g_cfg_rw.init_min   = -3.0;
  g_cfg_rw.init_max   = 1.0;
  cr_expect(epsilon_eq(dbl, velocity_cap(), 0.8, 1e-15));
}

Test(pso, velocity_cap_off) {
  PARAM("v_max") = 0.0;
  cr_expect(eq(dbl, velocity_cap(), 0.0));
}

// --- Loading ---

// An unset w_end is w: constant inertia
Test(pso, w_end_resolves_to_w) {
  Batch b = load_toml(
      "[common]\niterations = 5\n"
      "[[run]]\nalgorithm = \"pso\"\nw = 0.7\n"
      "[[run]]\nalgorithm = \"pso\"\nw = 0.7\nw_end = 0.4\n");
  cr_expect(eq(dbl, *(double*) algo_field(&b.runs[0], "w_end"), 0.7));
  cr_expect(eq(dbl, *(double*) algo_field(&b.runs[1], "w_end"), 0.4));
  free_batch(&b);
}

// --- Evaluation count ---

// One evaluation per particle at init, a second one for a random pbest, then one per particle per
// iteration done
static void check_count(const char* extra, int pbest_evaluations) {
  char toml[512];
  snprintf(toml, sizeof(toml),
           "[common]\nseed = 3\niterations = 25\n[[run]]\nalgorithm = \"pso\"\n%s", extra);
  Batch     b   = load_toml(toml);
  RunResult res = run_job_of(&b, 0);
  uint64_t  n   = (uint64_t) *(int*) algo_field(&b.runs[0], "swarm_size");
  cr_expect(eq(u64, res.evaluations, n * (1 + pbest_evaluations) + n * res.stop.iterations), "%s",
            extra);
  free(res.pos);
  free_batch(&b);
}

Test(pso, evaluation_count) {
  check_count("pbest_init = \"position\"\n", 0);
  check_count("pbest_init = \"random\"\n", 1);
  check_count("pbest_init = \"position\"\ntarget_fitness = 1e300\n", 0);
  check_count("pbest_init = \"random\"\ntarget_fitness = 1e300\n", 1);
}
