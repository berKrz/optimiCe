#include <stdlib.h>

#include "algorithms.h"
#include "common.h"

_Thread_local Config g_cfg;

const Algorithm algorithms[] = {
    {.name     = "pso",
     .fields   = pso_fields,
     .cfg_base = pso_cfg_base,
     .validate = pso_validate,
     .dump     = pso_dump,
     .run      = pso_run},
    {.name     = "ga",
     .fields   = ga_fields,
     .cfg_base = ga_cfg_base,
     .validate = ga_validate,
     .dump     = ga_dump,
     .run      = ga_run},
    {.name     = "sma",
     .fields   = sma_fields,
     .cfg_base = sma_cfg_base,
     .validate = sma_validate,
     .dump     = sma_dump,
     .run      = sma_run},
    {0},
};

int main(int argc, char** argv) {
  set_defaults(runner_fields, &g_cfg);
  parse_args(argc, argv);
  if (g_cfg.interactive) clear_screen();
  g_cfg.algorithm->run();

  return EXIT_SUCCESS;
}
