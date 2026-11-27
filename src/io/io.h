#pragma once

#include "core/core.h"

// --- Command line (args.c) ---

// What the command line asked main to do
typedef struct {
  const char* config_path;  // -f
  int         dump_config;  // -d
  int         output;       // -o was given
  const char* output_path;  // its argument; NULL when -o came bare: a name is generated
} Args;

// Parses the command line and nothing more: it loads no config and opens no file
// --interactive goes to g_opts, the rest is returned
// Answers --help and --list itself and exits; dies on an invalid command line
Args parse_args(int argc, char** argv);

// --- Config file (load.c) ---

// Parses, merges and validates every [[run]]; the TOML tree is freed on return
Batch load_batch(const char* filename);

// --- Results ---

// One finished job, as every results writer sees it: built once by the batch runner and handed to
// the terminal and the results file alike
typedef struct {
  const char*      label;      // the run's label, e.g. pso-1
  int              replicate;  // from 0
  uint32_t         seed;       // the seed the job ran with
  int              n_dims;     // length of res->pos
  const RunResult* res;
} JobView;

// --- Results file (output.c) ---

// output_open picks the path from `arg`:
//   NULL:                                      a generated name in the current directory
//   a trailing slash or an existing directory: a generated name inside it
//   anything else:                             that file
// It creates missing directories, prints the path to stderr and writes the header
// output_row writes one job's row. Every write is checked: a failure dies naming the file
// Without output_open, the other three do nothing
void output_open(const Batch* batch, const char* config_path, const char* arg);
void output_row(const JobView* job);
void output_flush(void);
void output_close(void);

// --- Terminal (report.c) ---

// Writes the shortest %g form of x, from 6 to 17 significant digits, that reads back as exactly x
// For final results, so a printed value identifies the double. Starting at 6 matches plain %g
// whenever that was already exact
#define FMT_EXACT_LEN 32
void fmt_exact(char* buf, double x);

// The result block of a single job; mentions an early stop when there was one
void print_result(const RunResult* r);

// Column widths of a batch's job lines, so every line of the batch aligns
typedef struct {
  int label_w;
  int rep_w;
  int iter_w;  // digits of the largest iteration budget in the batch
} JobLineWidths;

void          print_batch_header(const Batch* batch, int n_threads);
JobLineWidths job_line_widths(const Batch* batch);
void          print_job_line(const JobLineWidths* w, const JobView* job);
