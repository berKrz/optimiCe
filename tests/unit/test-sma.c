#include <criterion/criterion.h>
#include <criterion/new/assert.h>
#include <stdio.h>
#include <stdlib.h>

#include "helpers.h"

// --- Evaluation count ---

// One evaluation per slime at init, then one per slime per iteration done
static void check_count(const char* extra) {
  char toml[512];
  snprintf(toml, sizeof(toml),
           "[common]\nseed = 3\niterations = 25\n[[run]]\nalgorithm = \"sma\"\n%s", extra);
  Batch     b   = load_toml(toml);
  RunResult res = run_job_of(&b, 0);
  uint64_t  n   = (uint64_t) * (int*) algo_field(&b.runs[0], "pop_size");
  cr_expect(eq(u64, res.evaluations, n + n * res.stop.iterations), "%s", extra);
  free(res.pos);
  free_batch(&b);
}

Test(sma, evaluation_count) {
  check_count("");
  check_count("target_fitness = 1e300\n");
}

// --- Re-initialization ---

// With z = 1 every slime is redrawn every iteration, from the init range, so wherever the best was
// found is inside it. The optimum, 0, is in the domain but not in the init range: a redraw from
// the domain would soon beat every point the init range holds
Test(sma, redraws_come_from_init_range) {
  Batch b = load_toml(
      "[common]\nseed = 3\nn_dims = 3\niterations = 50\n"
      "domain_min = -10.0\ndomain_max = 10.0\ninit_min = 5.0\ninit_max = 6.0\n"
      "[[run]]\nalgorithm = \"sma\"\nz = 1.0\n");
  RunResult res = run_job_of(&b, 0);
  for (int d = 0; d < 3; d++)
    cr_expect(res.pos[d] >= 5.0 && res.pos[d] <= 6.0, "pos[%d] = %g", d, res.pos[d]);
  free(res.pos);
  free_batch(&b);
}
