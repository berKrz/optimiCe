#include <stdlib.h>

#include "algorithms.h"
#include "common.h"

_Thread_local Config g_cfg;

const Algorithm algorithms[] = {
    {.name = "pso", .fields = pso_fields, .dump = pso_dump, .run = pso_run},
    {.name = "ga", .fields = ga_fields, .dump = ga_dump, .run = ga_run, .validate = ga_validate},
    {.name = "sma", .fields = sma_fields, .dump = sma_dump, .run = sma_run},
    {0},
};

int main(int argc, char** argv) {
  set_defaults_from_table(common_fields);
  parse_args(argc, argv);
  if (g_cfg.interactive) clear_screen();
  g_cfg.algorithm->run();

  return EXIT_SUCCESS;
}
