#include <stdlib.h>

#include "algorithms.h"
#include "common.h"

_Thread_local Config g_cfg;

const Algorithm algorithms[] = {
    {.name = "pso", .fields = pso_fields, .run = pso_run},
    {0},
};

int main(int argc, char** argv) {
  set_defaults_from_table(common_fields);
  parse_args(argc, argv);
  if (!g_cfg.algorithm) die("algoritmo não definido no arquivo de configuração");
  if (g_cfg.interactive) clear_screen();
  g_cfg.algorithm->run();

  return EXIT_SUCCESS;
}
