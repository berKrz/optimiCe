#include <stdlib.h>

#include "algorithms.h"
#include "common.h"

_Thread_local Config g_cfg;

Options g_opts;

const Algorithm* const algorithms[] = {&pso_algorithm, &ga_algorithm, &sma_algorithm, NULL};

int main(int argc, char** argv) {
  set_defaults(runner_fields, &g_cfg);
  parse_args(argc, argv);
  if (g_opts.interactive) clear_screen();

  double* pos = malloc(g_cfg.n_dims * sizeof(double));
  if (!pos) die("memory allocation failed");

  RunResult result = {.pos = pos};
  g_cfg.algorithm->run(&result);
  print_result(&result);

  free(pos);
  return EXIT_SUCCESS;
}
