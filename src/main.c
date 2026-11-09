#include <stdlib.h>

#include "algorithms.h"
#include "common.h"

_Thread_local Config g_cfg;

Options g_opts;

const Algorithm* const algorithms[] = {&pso_algorithm, &ga_algorithm, &sma_algorithm, NULL};

int main(int argc, char** argv) {
  Batch batch = parse_args(argc, argv);
  if (g_opts.interactive) clear_screen();
  run_batch(&batch);
  free_batch(&batch);

  return EXIT_SUCCESS;
}
