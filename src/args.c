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
      "  -l, --list              List available algorithms and fitness functions\n"
      "  -d, --dump-config       Print the resolved configuration and exit\n"
      "  -o, --output [path]     Also write results as CSV: to a file, into a directory (path\n"
      "                          ending in /), or, without a path, to a generated name here\n"
      "  -h, --help [ALG]        General help, or help for a specific algorithm\n"
      "\n"
      "Short options work in either case (-F is -f).\n");
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
  die_with_hint("unknown algorithm: '%s'", name);
}

// Both lists come from where the names are defined: the registry and the fitness catalog
static void print_list(void) {
  printf("Algorithms:\n");
  for (int i = 0; algorithms[i]; i++) printf("  %s\n", algorithms[i]->name);
  printf("\nFitness functions:\n");
  fitness_print_catalog();
}

// Dies naming the option getopt just rejected, the way the user wrote it: `arg` is the argument
// it came from
// A long option is quoted whole (optopt can't name it: it's 0 for an unknown one), a short one by
// its letter, since it may sit in the middle of a cluster like -ix
static noreturn void die_option(const char* problem, const char* arg) {
  if (strncmp(arg, "--", 2) == 0) die_with_hint("%s: %s", problem, arg);
  die_with_hint("%s: -%c", problem, optopt);
}

Args parse_args(int argc, char** argv) {
  static const struct option long_options[] = {
      {"config",      required_argument, NULL, 'f'},
      {"interactive", no_argument,       NULL, 'i'},
      {"list",        no_argument,       NULL, 'l'},
      {"dump-config", no_argument,       NULL, 'd'},
      {"output",      optional_argument, NULL, 'o'},
      {"help",        no_argument,       NULL, 'h'},
      {NULL,          0,                 NULL, 0  },
  };

  Args args      = {0};
  int  want_help = 0;

  // Short options are accepted in either case
  // The leading ':' makes getopt return ':' for a missing argument, apart from '?' for the rest
  opterr = 0;
  int c;
  while ((c = getopt_long(argc, argv, ":f:F:iIlLdDo::O::hH", long_options, NULL)) != -1) {
    switch (c) {
      case 'f':
      case 'F': args.config_path = optarg; break;
      case 'i':
      case 'I': g_opts.interactive = 1; break;
      case 'l':
      case 'L': print_list(); exit(EXIT_SUCCESS);
      case 'd':
      case 'D': args.dump_config = 1; break;
      case 'o':
      case 'O':
        // --output takes an optional value, but getopt only attaches one written as --output=FILE
        // or -oFILE: with "-o FILE", FILE would be left over as a stray argument. So take the
        // next argument ourselves, unless it starts with '-' (it's the next option, so -o came
        // bare). A bare -o leaves output_path NULL: a name is generated in the current directory
        if (!optarg && optind < argc && argv[optind][0] != '-') optarg = argv[optind++];
        args.output      = 1;
        args.output_path = optarg;
        break;
      case 'h':
      case 'H': want_help = 1; break;

      case ':': die_option("missing argument for option", argv[optind - 1]);
      default:  die_option("invalid option", argv[optind - 1]);
    }
  }

  if (want_help) {
    if (optind < argc) print_help_algorithm(argv[optind]);
    else print_help_overview();
    exit(EXIT_SUCCESS);
  }

  if (optind < argc) die_with_hint("unexpected argument");
  if (!args.config_path) die_with_hint("--config / -f is required");

  return args;
}
