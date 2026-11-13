#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "algorithms.h"
#include "common.h"

void wait_for_enter(void) {
  if (!g_opts.interactive) return;
  printf("\n  [Press Enter to continue...]\n");
  while (getchar() != '\n');
}

void print_separator(void) {
  printf("\n");
  for (int i = 0; i < LINE_WIDTH / 2; i++) printf("- ");
  printf("\n\n");
}

void print_iteration_header(int iter) {
  char buf[32];
  int  n = snprintf(buf, sizeof(buf), "=== Iteration %d ", iter);
  printf("%s", buf);
  for (int i = n; i < LINE_WIDTH; i++) printf("=");
  printf("\n\n");
}

void fmt_exact(char* buf, double x) {
  // %.17g always reads back exactly; NaN never compares equal, so it falls through to there
  for (int p = 6; p <= 17; p++) {
    snprintf(buf, FMT_EXACT_LEN, "%.*g", p, x);
    if (strtod(buf, NULL) == x) return;
  }
}

void print_result(const RunResult* r) {
  for (int i = 0; i < LINE_WIDTH; i++) printf("=");
  printf("\n");

  char buf[32];
  int  n = snprintf(buf, sizeof(buf), "=== Final Result ");
  printf("%s", buf);
  for (int i = n; i < LINE_WIDTH; i++) printf("=");
  printf("\n\n");

  printf("Best solution:\n");
  printf("   pos: [ ");
  for (int d = 0; d < g_cfg.n_dims; d++) printf("%7.3f ", r->pos[d]);
  printf("]\n");
  char fit[FMT_EXACT_LEN];
  fmt_exact(fit, r->fitness);
  printf("   fitness = %s\n\n", fit);

  if (r->stop.reason == STOP_TARGET)
    printf("Stopped early: target fitness %g reached\n\n", g_cfg.target_fitness);
  else if (r->stop.reason == STOP_STAGNATION)
    printf("Stopped early: no improvement for %d iterations\n\n", g_cfg.stagnation);

  printf("Iterations: %d\n", r->stop.iterations);
  printf("Evaluations: %" PRIu64 "\n", r->evaluations);
  printf("Algorithm: %s    Seed: 0x%08x\n", g_cfg.algorithm->name, g_cfg.seed);
  for (int i = 0; i < LINE_WIDTH; i++) printf("=");
  printf("\n");
}

void print_vec(const double* v) {
  printf("[ ");
  for (int d = 0; d < g_cfg.n_dims; d++) printf("%7.3f ", v[d]);
  printf("]");
}

// --- Batch output ---

void print_batch_header(const Batch* batch, int n_threads) {
  printf("Batch: %d run%s x %d replicate%s = %d jobs    Base seed: 0x%08x\n", batch->n_runs,
         batch->n_runs == 1 ? "" : "s", batch->replicates, batch->replicates == 1 ? "" : "s",
         batch_jobs(batch), batch->seed);
  // The thread count depends on the machine, so it stays out of stdout
  fflush(stdout);
  fprintf(stderr, "Threads: %d\n", n_threads);
  printf("\n");
}

static int digits(int n) {
  int d = 1;
  while (n >= 10) n /= 10, d++;
  return d;
}

JobLineWidths job_line_widths(const Batch* batch) {
  JobLineWidths w = {.rep_w = digits(batch->replicates - 1)};
  for (int k = 0; k < batch->n_runs; k++) {
    int len = (int) strlen(batch->runs[k].label);
    if (len > w.label_w) w.label_w = len;
    len = digits(batch->runs[k].cfg.iterations);
    if (len > w.iter_w) w.iter_w = len;
  }
  return w;
}

void print_job_line(const JobLineWidths* w, const char* label, int replicate, uint32_t seed,
                    const RunResult* res) {
  char fit[FMT_EXACT_LEN], best[FMT_EXACT_LEN + 8], evals[32];
  fmt_exact(fit, res->fitness);
  snprintf(best, sizeof(best), "best = %s", fit);
  snprintf(evals, sizeof(evals), "evals %" PRIu64, res->evaluations);

  // best is padded to its longest form, "best = " and 24 characters: -1.2345678901234567e-308
  printf("%-*s  r%-*d  seed 0x%08x  %-31s  iters %-*d  ", w->label_w, label, w->rep_w, replicate,
         seed, best, w->iter_w, res->stop.iterations);
  switch (res->stop.reason) {
    case STOP_NONE:       printf("%s\n", evals); break;
    case STOP_TARGET:     printf("%-16s  target\n", evals); break;
    case STOP_STAGNATION: printf("%-16s  stagnation\n", evals); break;
  }
}
