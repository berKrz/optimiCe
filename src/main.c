#include <stdlib.h>

#include "common.h"

int main(int argc, char** argv) {
  Args  args  = parse_args(argc, argv);
  Batch batch = load_batch(args.config_path);
  int   jobs  = batch_jobs(&batch);

  // Checked before the dump: --dump-config only prints a batch that would run
  if (g_opts.interactive && jobs > 1)
    die_with_hint("--interactive needs a single job (got %d run%s x %d replicate%s = %d)",
                  batch.n_runs, batch.n_runs == 1 ? "" : "s", batch.replicates,
                  batch.replicates == 1 ? "" : "s", jobs);
  g_opts.per_iteration = jobs == 1;

  if (args.dump_config) {
    dump_batch(&batch);
  } else {
    // Opened before the first job, so a bad path fails before any work; never on the dump path,
    // so --dump-config doesn't create or truncate a file
    if (args.output) output_open(&batch, args.config_path, args.output_path);
    run_batch(&batch);
    output_close();
  }

  free_batch(&batch);
  return EXIT_SUCCESS;
}
