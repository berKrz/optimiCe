#include <math.h>
#include <stdio.h>

#include "common.h"

int is_better(double a, double b) { return g_cfg.direction == MINIMIZE ? a < b : a > b; }

StopState stop_init(void) {
  return (StopState) {
      // Nothing seen yet: worse than any fitness, an infinite one included
      .best       = g_cfg.direction == MINIMIZE ? INFINITY : -INFINITY,
      .stale      = 0,
      .iterations = 0,
      .reason     = STOP_NONE,
  };
}

int end_iteration(StopState* st, double current_best) {
  // Progress lines are for a batch of one job, and not under --interactive, which prints its own
  // view of every iteration
  if (g_opts.per_iteration && !g_opts.interactive)
    printf("Iteration %3d   best = %g\n", st->iterations, current_best);
  st->iterations++;

  if (is_better(current_best, st->best)) {
    st->best  = current_best;
    st->stale = 0;
  } else {
    st->stale++;
  }

  // Target reached: at least as good as target_fitness
  if (!isnan(g_cfg.target_fitness) && !is_better(g_cfg.target_fitness, current_best))
    st->reason = STOP_TARGET;
  else if (g_cfg.stagnation > 0 && st->stale >= g_cfg.stagnation) st->reason = STOP_STAGNATION;

  return st->reason != STOP_NONE;
}
