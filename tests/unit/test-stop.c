#include <criterion/criterion.h>
#include <criterion/new/assert.h>
#include <math.h>

#include "unittest.h"

// Minimizing, with both early stops off and an iteration cap no test reaches
static void setup(void) {
  g_cfg_rw.direction      = MINIMIZE;
  g_cfg_rw.target_fitness = NAN;
  g_cfg_rw.stagnation     = 0;
  g_cfg_rw.iterations     = 1000000;
}

TestSuite(stop, .init = setup);

static void expect_state(const StopState* st, double best, int stale, int iterations) {
  cr_expect(eq(dbl, st->best, best));
  cr_expect(eq(int, st->stale, stale));
  cr_expect(eq(int, st->iterations, iterations));
}

// --- Best and stale ---

// An improvement resets stale; an equal or worse value counts as stale and leaves best alone
Test(stop, improvement_and_stale) {
  StopState st = stop_init();
  cr_expect(eq(int, end_iteration(&st, 5.0), 0));
  expect_state(&st, 5.0, 0, 1);
  end_iteration(&st, 7.0);
  expect_state(&st, 5.0, 1, 2);
  end_iteration(&st, 5.0);
  expect_state(&st, 5.0, 2, 3);
  end_iteration(&st, 4.0);
  expect_state(&st, 4.0, 0, 4);
  cr_expect(eq(int, st.reason, STOP_PENDING));
}

Test(stop, improvement_and_stale_maximizing) {
  g_cfg_rw.direction = MAXIMIZE;
  StopState st       = stop_init();
  end_iteration(&st, 5.0);
  expect_state(&st, 5.0, 0, 1);
  end_iteration(&st, 3.0);
  expect_state(&st, 5.0, 1, 2);
  end_iteration(&st, 6.0);
  expect_state(&st, 6.0, 0, 3);
}

// A first iteration whose best is as bad as it gets doesn't improve on "nothing seen yet": it
// counts as stale
Test(stop, infinite_first_iteration_is_stale) {
  g_cfg_rw.stagnation = 1;
  StopState st        = stop_init();
  cr_expect(eq(int, end_iteration(&st, INFINITY), 1));
  expect_state(&st, INFINITY, 1, 1);
  cr_expect(eq(int, st.reason, STOP_STAGNATION));

  g_cfg_rw.direction = MAXIMIZE;
  st                 = stop_init();
  cr_expect(eq(int, end_iteration(&st, -INFINITY), 1));
  expect_state(&st, -INFINITY, 1, 1);
  cr_expect(eq(int, st.reason, STOP_STAGNATION));
}

// --- Target ---

// Reaching the target exactly counts
Test(stop, target_is_inclusive) {
  g_cfg_rw.target_fitness = 1.0;
  StopState st            = stop_init();
  cr_expect(eq(int, end_iteration(&st, 1.5), 0));
  cr_expect(eq(int, end_iteration(&st, 1.0), 1));
  cr_expect(eq(int, st.reason, STOP_TARGET));
}

Test(stop, target_passed) {
  g_cfg_rw.target_fitness = 1.0;
  StopState st            = stop_init();
  cr_expect(eq(int, end_iteration(&st, 0.5), 1));
  cr_expect(eq(int, st.reason, STOP_TARGET));
}

Test(stop, target_maximizing) {
  g_cfg_rw.direction      = MAXIMIZE;
  g_cfg_rw.target_fitness = 10.0;
  StopState st            = stop_init();
  cr_expect(eq(int, end_iteration(&st, 9.0), 0));
  cr_expect(eq(int, end_iteration(&st, 10.0), 1));
  cr_expect(eq(int, st.reason, STOP_TARGET));

  st = stop_init();
  cr_expect(eq(int, end_iteration(&st, 12.0), 1));
}

Test(stop, nan_target_is_off) {
  StopState st = stop_init();
  for (int i = 0; i < 100; i++)
    cr_assert(eq(int, end_iteration(&st, -1e6 * i), 0), "stopped at iteration %d", i);
  cr_expect(eq(int, st.reason, STOP_PENDING));
}

// --- Stagnation ---

// Stops on the stagnation-th iteration in a row without improving, not before
Test(stop, stagnation_boundary) {
  g_cfg_rw.stagnation = 3;
  StopState st        = stop_init();
  cr_expect(eq(int, end_iteration(&st, 5.0), 0));
  cr_expect(eq(int, end_iteration(&st, 5.0), 0));
  cr_expect(eq(int, end_iteration(&st, 6.0), 0));
  cr_expect(eq(int, end_iteration(&st, 5.0), 1));
  cr_expect(eq(int, st.reason, STOP_STAGNATION));
  expect_state(&st, 5.0, 3, 4);
}

Test(stop, improvement_restarts_stagnation) {
  g_cfg_rw.stagnation = 3;
  StopState st        = stop_init();
  end_iteration(&st, 5.0);
  end_iteration(&st, 5.0);
  end_iteration(&st, 5.0);
  cr_expect(eq(int, end_iteration(&st, 4.0), 0));
  cr_expect(eq(int, end_iteration(&st, 4.0), 0));
  cr_expect(eq(int, end_iteration(&st, 4.0), 0));
  cr_expect(eq(int, end_iteration(&st, 4.0), 1));
}

Test(stop, zero_stagnation_is_off) {
  StopState st = stop_init();
  for (int i = 0; i < 1000; i++)
    cr_assert(eq(int, end_iteration(&st, 5.0), 0), "stopped at iteration %d", i);
  cr_expect(eq(int, st.stale, 999));
}

// --- Iteration cap ---

// Stops once the cap's iterations are done, not before
Test(stop, iterations_cap) {
  g_cfg_rw.iterations = 3;
  StopState st        = stop_init();
  cr_expect(eq(int, end_iteration(&st, 5.0), 0));
  cr_expect(eq(int, end_iteration(&st, 5.0), 0));
  cr_expect(eq(int, end_iteration(&st, 5.0), 1));
  cr_expect(eq(int, st.reason, STOP_ITERATIONS));
  cr_expect(eq(int, st.iterations, 3));
}

// An early stop on the last iteration is the reason, not the cap
Test(stop, target_wins_on_last_iteration) {
  g_cfg_rw.iterations     = 2;
  g_cfg_rw.target_fitness = 1.0;
  StopState st            = stop_init();
  end_iteration(&st, 5.0);
  cr_expect(eq(int, end_iteration(&st, 1.0), 1));
  cr_expect(eq(int, st.reason, STOP_TARGET));
}

Test(stop, stagnation_wins_on_last_iteration) {
  g_cfg_rw.iterations = 2;
  g_cfg_rw.stagnation = 1;
  StopState st        = stop_init();
  end_iteration(&st, 5.0);
  cr_expect(eq(int, end_iteration(&st, 5.0), 1));
  cr_expect(eq(int, st.reason, STOP_STAGNATION));
}

// --- Progress ---

// The current iteration counts as underway: 1/4 on the first of 4, exactly 1 on the last
Test(stop, progress_counts_current_iteration) {
  g_cfg_rw.iterations = 4;
  StopState st        = stop_init();
  cr_expect(eq(dbl, progress(&st), 0.25));
  end_iteration(&st, 5.0);
  cr_expect(eq(dbl, progress(&st), 0.5));
  end_iteration(&st, 5.0);
  end_iteration(&st, 5.0);
  cr_expect(eq(dbl, progress(&st), 1.0));
}
