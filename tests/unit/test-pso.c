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
  g_cfg_rw.iterations = 11;
}

TestSuite(pso, .init = setup);

#define PARAM(key) (*(double*) live_field("pso", key))

// --- Inertia ---

// Linear from w at the first iteration to w_end at the last
Test(pso, inertia_schedule) {
  PARAM("w")     = 0.9;
  PARAM("w_end") = 0.4;
  cr_expect(eq(dbl, inertia(0), 0.9));
  cr_expect(epsilon_eq(dbl, inertia(5), 0.65, 1e-15));
  cr_expect(epsilon_eq(dbl, inertia(10), 0.4, 1e-15));
}

// No division by iterations - 1 = 0
Test(pso, inertia_single_iteration) {
  PARAM("w")          = 0.9;
  PARAM("w_end")      = 0.4;
  g_cfg_rw.iterations = 1;
  cr_expect(eq(dbl, inertia(0), 0.9));
}

// Constant inertia stays bit-identical to w
Test(pso, inertia_constant) {
  PARAM("w")     = 0.7;
  PARAM("w_end") = 0.7;
  for (int i = 0; i < 11; i++) cr_expect(eq(dbl, inertia(i), 0.7), "iteration %d", i);
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
  uint64_t  n   = (uint64_t) * (int*) algo_field(&b.runs[0], "swarm_size");
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
