#include <criterion/criterion.h>
#include <criterion/new/assert.h>
#include <math.h>
#include <stdlib.h>

#include "helpers.h"

// The runner fields as set_defaults leaves them, to compare against without restating defaults
static Config runner_defaults(void) {
  Config saved = g_cfg_rw, defaults;
  set_defaults(runner_fields, &g_cfg_rw);
  defaults = g_cfg;
  g_cfg_rw = saved;
  return defaults;
}

// The int algorithm field `key` as `algo`'s defaults set it
static int default_algo_int(const char* algo_name, const char* key) {
  const Algorithm* algo = algorithms[find_algorithm(algo_name)];
  void*            base = calloc(1, algo->cfg_size);
  set_defaults(algo->fields, base);
  int v = *(int*) algo_field_in(algo, base, key);
  free(base);
  return v;
}

// --- Labels ---

// One counter per algorithm, in file order
Test(load, labels) {
  Batch b = load_toml(
      "[common]\niterations = 5\n"
      "[[run]]\nalgorithm = \"pso\"\n"
      "[[run]]\nalgorithm = \"pso\"\n"
      "[[run]]\nalgorithm = \"ga\"\n"
      "[[run]]\nalgorithm = \"sma\"\n"
      "[[run]]\nalgorithm = \"pso\"\n");
  const char* expected[] = {"pso-1", "pso-2", "ga-1", "sma-1", "pso-3"};
  cr_assert(eq(int, b.n_runs, 5));
  for (int i = 0; i < 5; i++) cr_expect(eq(str, b.runs[i].label, (char*) expected[i]));
  free_batch(&b);
}

// --- Precedence ---

// defaults, then [common], then the run's own value, which the next run doesn't see
Test(load, runner_precedence) {
  Batch b = load_toml(
      "[common]\nn_dims = 5\niterations = 5\n"
      "[[run]]\nalgorithm = \"pso\"\n"
      "[[run]]\nalgorithm = \"pso\"\nn_dims = 7\n"
      "[[run]]\nalgorithm = \"pso\"\n");
  cr_expect(eq(int, b.runs[0].cfg.n_dims, 5));
  cr_expect(eq(int, b.runs[1].cfg.n_dims, 7));
  cr_expect(eq(int, b.runs[2].cfg.n_dims, 5));
  cr_expect(eq(int, b.runs[0].cfg.stagnation, runner_defaults().stagnation));
  free_batch(&b);
}

// defaults, then [defaults.<algo>], then the run's own value, which the next run doesn't see
// [defaults.ga] sets a key sma has too, and sma doesn't see it
Test(load, algorithm_precedence) {
  Batch b = load_toml(
      "[common]\niterations = 5\n"
      "[defaults.pso]\nswarm_size = 40\n"
      "[defaults.ga]\npop_size = 20\n"
      "[[run]]\nalgorithm = \"pso\"\n"
      "[[run]]\nalgorithm = \"pso\"\nswarm_size = 50\n"
      "[[run]]\nalgorithm = \"pso\"\n"
      "[[run]]\nalgorithm = \"sma\"\n"
      "[[run]]\nalgorithm = \"ga\"\n");
  cr_expect(eq(int, *(int*) algo_field(&b.runs[0], "swarm_size"), 40));
  cr_expect(eq(int, *(int*) algo_field(&b.runs[1], "swarm_size"), 50));
  cr_expect(eq(int, *(int*) algo_field(&b.runs[2], "swarm_size"), 40));
  cr_expect(
      eq(int, *(int*) algo_field(&b.runs[3], "pop_size"), default_algo_int("sma", "pop_size")));
  cr_expect(eq(int, *(int*) algo_field(&b.runs[4], "pop_size"), 20));
  free_batch(&b);
}

// A [defaults.<algo>] block no run uses isn't read, so even an unknown key in it loads
Test(load, unused_defaults_not_read) {
  Batch b = load_toml(
      "[common]\niterations = 5\n"
      "[defaults.sma]\nno_such_key = 1\n"
      "[[run]]\nalgorithm = \"pso\"\n");
  cr_expect(eq(int, b.n_runs, 1));
  free_batch(&b);
}

// --- Seed and replicates ---

Test(load, given_seed) {
  Batch b = load_toml(
      "[common]\nseed = 1234\nreplicates = 3\niterations = 5\n"
      "[[run]]\nalgorithm = \"pso\"\n"
      "[[run]]\nalgorithm = \"ga\"\n");
  cr_expect(eq(u32, b.seed, 1234));
  cr_expect(eq(int, b.seed_generated, 0));
  cr_expect(eq(int, b.replicates, 3));
  for (int i = 0; i < 2; i++) cr_expect(eq(u32, b.runs[i].cfg.seed, 1234));
  free_batch(&b);
}

// Omitted, a seed is generated, and every run has it as its base
Test(load, generated_seed) {
  Batch b = load_toml(
      "[common]\nn_dims = 3\niterations = 5\n"
      "[[run]]\nalgorithm = \"pso\"\n"
      "[[run]]\nalgorithm = \"ga\"\n");
  cr_expect(eq(int, b.seed_generated, 1));
  for (int i = 0; i < 2; i++) cr_expect(eq(u32, b.runs[i].cfg.seed, b.seed));
  free_batch(&b);
}

Test(load, generated_seed_without_common) {
  Batch b = load_toml("[[run]]\nalgorithm = \"pso\"\niterations = 5\n");
  cr_expect(eq(int, b.seed_generated, 1));
  cr_expect(eq(u32, b.runs[0].cfg.seed, b.seed));
  free_batch(&b);
}

// --- Optional fields ---

// nan in a run turns off what [common] set, for that run only
Test(load, nan_turns_optional_off) {
  Batch b = load_toml(
      "[common]\ntarget_fitness = 1.0\niterations = 5\n"
      "[[run]]\nalgorithm = \"pso\"\ntarget_fitness = nan\n"
      "[[run]]\nalgorithm = \"pso\"\n");
  cr_expect(isnan(b.runs[0].cfg.target_fitness));
  cr_expect(eq(dbl, b.runs[1].cfg.target_fitness, 1.0));
  free_batch(&b);
}

// --- Init range ---

// An unset end of the init range is the domain's
Test(load, init_range_resolves_to_domain) {
  Batch b = load_toml(
      "[common]\ndomain_min = -2.0\ndomain_max = 3.0\niterations = 5\n"
      "[[run]]\nalgorithm = \"pso\"\n"
      "[[run]]\nalgorithm = \"pso\"\ninit_min = 0.0\n");
  cr_expect(eq(dbl, b.runs[0].cfg.init_min, -2.0));
  cr_expect(eq(dbl, b.runs[0].cfg.init_max, 3.0));
  cr_expect(eq(dbl, b.runs[1].cfg.init_min, 0.0));
  cr_expect(eq(dbl, b.runs[1].cfg.init_max, 3.0));
  free_batch(&b);
}
