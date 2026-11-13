#pragma once

// Input and output: the command line, the config file, the terminal and the results file

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
// --interactive goes to g_opts, the rest is returned. --help and --list are answered here and
// exit; an invalid command line dies
Args parse_args(int argc, char** argv);

// --- Config file (load.c) ---

// Parses, merges and validates every [[run]]; the TOML tree is freed on return
Batch load_batch(const char* filename);

// --- Results file (output.c) ---

// output_open picks the path from `arg` (NULL: a generated name in the current directory; a
// trailing slash or an existing directory: a generated name inside it; anything else: that file),
// creates missing directories, prints the path to stderr and writes the header and column row;
// output_row writes one job's row
// Every write is checked: a failure dies naming the file
// Without output_open, the others do nothing
void output_open(const Batch* batch, const char* config_path, const char* arg);
void output_row(const char* label, int replicate, uint32_t seed, int n_dims, const RunResult* res);
void output_flush(void);
void output_close(void);

// --- Terminal (report.c) ---

// Writes the shortest %g form of x, at 6 to 17 significant digits, that reads back as exactly x
// Used for final results, so a printed value identifies the double; starting at 6 means it
// matches plain %g whenever %g was already exact
#define FMT_EXACT_LEN 32
void fmt_exact(char* buf, double x);

// Footer of a run; mentions an early stop when the result records one
void print_result(const RunResult* r);

// Column widths of a batch's job lines, so every line of the batch aligns
typedef struct {
  int label_w;
  int rep_w;
  int iter_w;  // digits of the largest iteration budget in the batch
} JobLineWidths;

void          print_batch_header(const Batch* batch, int n_threads);
JobLineWidths job_line_widths(const Batch* batch);
void print_job_line(const JobLineWidths* w, const char* label, int replicate, uint32_t seed,
                    const RunResult* res);
