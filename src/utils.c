#include <inttypes.h>
#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "algorithms.h"
#include "common.h"

const char* enum_name(const EnumEntry* table, int value) {
  for (int i = 0; table[i].name; i++)
    if (table[i].value == value) return table[i].name;
  return "?";
}

_Thread_local const char* g_where;

// Several worker threads may fail at once, but exit() must only run once: the first caller
// reports and exits, which ends every thread; any later caller blocks until that happens
static atomic_flag exiting = ATOMIC_FLAG_INIT;

static void claim_exit(void) {
  if (atomic_flag_test_and_set(&exiting))
    for (;;) pause();
}

// The one exit path behind the die family: "Error[ at '<source>'[ [<key>]]]: <message>", then
// the --help hint when asked for
static noreturn void die_va(const char* source, const char* key, int hint, const char* fmt,
                            va_list args) {
  claim_exit();
  fprintf(stderr, "Error");
  if (source) fprintf(stderr, " at '%s'", source);
  if (key) fprintf(stderr, " [%s]", key);
  fprintf(stderr, ": ");
  vfprintf(stderr, fmt, args);
  fprintf(stderr, "\n");
  if (hint) fprintf(stderr, "Run with --help to see available options.\n");
  exit(EXIT_FAILURE);
}

void die(const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  die_va(g_where, NULL, 0, fmt, args);
}

void die_with_hint(const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  die_va(g_where, NULL, 1, fmt, args);
}

void die_at(const char* source, const char* key, const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  die_va(source, key, 1, fmt, args);
}

void* xmalloc(size_t size) {
  void* p = malloc(size);
  if (!p) die("memory allocation failed");
  return p;
}

void* xcalloc(size_t n, size_t size) {
  void* p = calloc(n, size);
  if (!p) die("memory allocation failed");
  return p;
}

void check_fits_int(int count, long long per_item, const char* what) {
  if (per_item <= INT_MAX && count <= INT_MAX / per_item) return;
  die_with_hint("%s is too large (at most %d elements per buffer)", what, INT_MAX);
}

void clear_screen(void) {
  printf("\033[2J\033[H");
  fflush(stdout);
}

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

void print_iteration_line(int iter, double best_fitness) {
  if (!g_opts.per_iteration) return;
  printf("Iteration %3d   best = %g\n", iter, best_fitness);
}

StopState stop_init(void) {
  return (StopState) {
      // Nothing seen yet: worse than any fitness, an infinite one included
      .best   = g_cfg.direction == MINIMIZE ? INFINITY : -INFINITY,
      .stale  = 0,
      .reason = STOP_NONE,
  };
}

int stop_check(StopState* st, int iter, double current_best) {
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

  if (st->reason == STOP_NONE) return 0;
  st->iteration = iter;
  return 1;
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
    printf("Stopped early at iteration %d: target fitness %g reached\n\n", r->stop.iteration,
           g_cfg.target_fitness);
  else if (r->stop.reason == STOP_STAGNATION)
    printf("Stopped early at iteration %d: no improvement for %d iterations\n\n", r->stop.iteration,
           g_cfg.stagnation);

  printf("Iterations: %d\n", r->iterations);
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

int is_better(double a, double b) { return g_cfg.direction == MINIMIZE ? a < b : a > b; }
