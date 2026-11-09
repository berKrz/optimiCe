#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "algorithms.h"
#include "common.h"

// One group per distinct scope, in order of first appearance in runner_fields
static void print_runner_fields(void) {
  for (int i = 0; runner_fields[i].name; i++) {
    unsigned scope = runner_fields[i].scope;
    int      seen  = 0;
    for (int j = 0; j < i; j++) seen |= runner_fields[j].scope == scope;
    if (seen) continue;

    char where[64];
    scope_text(scope, where, sizeof(where));
    int single   = (scope & (scope - 1)) == 0;  // exactly one section bit
    int override = (scope & SCOPE_GLOBAL) == SCOPE_GLOBAL;
    printf("\nFields in %s%s:\n", where,
           single   ? " only" :
           override ? " (a [[run]] value overrides [common])" :
                      "");
    print_help_from_table(runner_fields, scope);
  }
}

static void print_help_overview(void) {
  printf(
      "Usage: optimice -f <file.toml> [options]\n"
      "\n"
      "Options:\n"
      "  -f, --config <file>     TOML configuration file (required)\n"
      "  -i, --interactive       Pause after each iteration (single job only)\n"
      "  -l, --list              List available algorithms\n"
      "  -d, --dump-config       Print the resolved configuration and exit\n"
      "  -h, --help [ALG]        General help, or help for a specific algorithm\n");
  print_runner_fields();
  printf("\nAlgorithms:\n");
  for (int i = 0; algorithms[i]; i++) printf("  %s\n", algorithms[i]->name);
  printf("\nUse --help ALG to see an algorithm's fields.\n");
}

static void print_help_algorithm(const char* name) {
  for (int i = 0; algorithms[i]; i++) {
    if (strcmp(name, algorithms[i]->name) == 0) {
      char where[64];
      scope_text(SCOPE_ALGO, where, sizeof(where));
      int single = (SCOPE_ALGO & (SCOPE_ALGO - 1)) == 0;
      printf("Fields for algorithm '%s' (%s%s%s):\n", name, single ? "" : "in ", where,
             single ? " only" : "");
      print_help_from_table(algorithms[i]->fields, SCOPE_ALGO);
      return;
    }
  }
  char msg[128];
  snprintf(msg, sizeof(msg), "unknown algorithm: '%s'", name);
  die(msg);
}

static void print_list(void) {
  for (int i = 0; algorithms[i]; i++) printf("%s\n", algorithms[i]->name);
}

Batch parse_args(int argc, char** argv) {
  static const struct option long_options[] = {
      {"config",      required_argument, NULL, 'f'},
      {"interactive", no_argument,       NULL, 'i'},
      {"list",        no_argument,       NULL, 'l'},
      {"dump-config", no_argument,       NULL, 'd'},
      {"help",        no_argument,       NULL, 'h'},
      {NULL,          0,                 NULL, 0  },
  };

  const char* config_path = NULL;
  int         want_help   = 0;
  int         want_config = 0;

  opterr = 0;
  int c;
  while ((c = getopt_long(argc, argv, "f:F:iIlLdDhH", long_options, NULL)) != -1) {
    switch (c) {
      case 'f':
      case 'F': config_path = optarg; break;
      case 'i':
      case 'I': g_opts.interactive = 1; break;
      case 'l':
      case 'L': print_list(); exit(EXIT_SUCCESS);
      case 'd':
      case 'D': want_config = 1; break;
      case 'h':
      case 'H': want_help = 1; break;

      default: {
        char msg[64];
        snprintf(msg, sizeof(msg), "invalid option: -%c", optopt);
        die(msg);
      }
    }
  }

  if (want_help) {
    if (optind < argc) print_help_algorithm(argv[optind]);
    else print_help_overview();
    exit(EXIT_SUCCESS);
  }

  if (optind < argc) die("unexpected argument");
  if (!config_path) die("--config / -f is required");

  Batch batch = load_batch(config_path);
  int   jobs  = batch_jobs(&batch);

  // Checked before the dump: --dump-config only prints configs that would run
  if (g_opts.interactive && jobs > 1) {
    char msg[160];
    snprintf(msg, sizeof(msg),
             "--interactive needs a single job (got %d run%s x %d replicate%s = %d)", batch.n_runs,
             batch.n_runs == 1 ? "" : "s", batch.replicates, batch.replicates == 1 ? "" : "s",
             jobs);
    die(msg);
  }
  g_opts.per_iteration = jobs == 1;

  if (want_config) {
    dump_batch(&batch);
    exit(EXIT_SUCCESS);
  }
  return batch;
}
