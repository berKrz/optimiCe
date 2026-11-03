#include <stdio.h>
#include <stdlib.h>

#include "algorithms.h"
#include "common.h"

void die(const char* msg) {
  fprintf(stderr, "Error: %s\n", msg);
  fprintf(stderr, "Run with --help to see available options.\n");
  exit(EXIT_FAILURE);
}

void die_at(const char* source, const char* key, const char* msg) {
  if (key) fprintf(stderr, "Error at '%s' [%s]: %s\n", source, key, msg);
  else fprintf(stderr, "Error at '%s': %s\n", source, msg);
  fprintf(stderr, "Run with --help to see available options.\n");
  exit(EXIT_FAILURE);
}

void clear_screen(void) {
  printf("\033[2J\033[H");
  fflush(stdout);
}

void wait_for_enter(void) {
  if (!g_cfg.interactive) return;
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
  printf("Iteration %3d   best = %g\n", iter, best_fitness);
}

void print_result(const double* best_pos, double best_fitness) {
  for (int i = 0; i < LINE_WIDTH; i++) printf("=");
  printf("\n");

  char buf[32];
  int  n = snprintf(buf, sizeof(buf), "=== Final Result ");
  printf("%s", buf);
  for (int i = n; i < LINE_WIDTH; i++) printf("=");
  printf("\n\n");

  printf("Best solution:\n");
  printf("   pos: [ ");
  for (int d = 0; d < g_cfg.n_dims; d++) printf("%7.3f ", best_pos[d]);
  printf("]\n");
  printf("   fitness = %g\n\n", best_fitness);

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
