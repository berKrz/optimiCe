#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "algorithms.h"
#include "common.h"

static void print_help_overview(void) {
  printf(
      "Usage: optimice -f <file.ini> [options]\n"
      "\n"
      "Options:\n"
      "  -f, --config <file>     INI configuration file (required)\n"
      "  -i, --interactive       Pause after each iteration\n"
      "  -l, --list              List available algorithms\n"
      "  -d, --dump-config       Serialize g_cfg after parsing and exit\n"
      "  -h, --help [ALG]        General help, or help for a specific algorithm\n"
      "\n"
      "Common fields (valid for any algorithm):\n");
  print_help_from_table(common_fields);
  printf("\nAlgorithms:\n");
  for (int i = 0; algorithms[i].name; i++) printf("  %s\n", algorithms[i].name);
  printf("\nUse --help ALG to see an algorithm's fields.\n");
}

static void print_help_algorithm(const char* name) {
  for (int i = 0; algorithms[i].name; i++) {
    if (strcmp(name, algorithms[i].name) == 0) {
      printf("Fields for algorithm '%s' (configure via INI):\n", name);
      print_help_from_table(algorithms[i].fields);
      return;
    }
  }
  char msg[128];
  snprintf(msg, sizeof(msg), "unknown algorithm: '%s'", name);
  die(msg);
}

static void print_list(void) {
  for (int i = 0; algorithms[i].name; i++) printf("%s\n", algorithms[i].name);
}

void parse_args(int argc, char** argv) {
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
  int         dump_config = 0;

  opterr = 0;
  int c;
  while ((c = getopt_long(argc, argv, "f:F:iIlLdDhH", long_options, NULL)) != -1) {
    switch (c) {
      case 'f':
      case 'F': config_path = optarg; break;
      case 'i':
      case 'I': g_cfg.interactive = 1; break;
      case 'l':
      case 'L': print_list(); exit(EXIT_SUCCESS);
      case 'd':
      case 'D': dump_config = 1; break;
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

  parse_ini(config_path);
  validate();
  rng_init_auto();

  if (dump_config) {
    dump_config_fn();
    exit(EXIT_SUCCESS);
  }
}
