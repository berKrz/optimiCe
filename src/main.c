#include <stdlib.h>

#include "algorithms.h"
#include "common.h"

_Thread_local Config g_cfg;

const Algorithm algorithms[] = {
    {"pso", ALGO_PSO, pso_fields, pso_run},
    {NULL,  0,        NULL,       NULL   },
};

int main(int argc, char** argv) {
  set_defaults_from_table(common_fields);
  parse_args(argc, argv);
  if (!g_cfg.algorithm) die("algoritmo não definido no arquivo de configuração");
  clear_screen();
  g_cfg.algorithm->run();

  return EXIT_SUCCESS;
}
