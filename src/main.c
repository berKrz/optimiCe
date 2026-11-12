#include <stdlib.h>

#include "common.h"

_Thread_local Config g_cfg;

Options g_opts;

int main(int argc, char** argv) {
  Batch batch = parse_args(argc, argv);
  if (g_opts.interactive) clear_screen();
  run_batch(&batch);
  output_close();
  free_batch(&batch);

  return EXIT_SUCCESS;
}
