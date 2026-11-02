#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "algorithms.h"
#include "common.h"

static void print_help_overview(void) {
  printf(
      "Uso: optimice -f <arquivo.ini> [opções]\n"
      "\n"
      "Opções:\n"
      "  -f, --config <arquivo>  Arquivo INI de configuração (obrigatório)\n"
      "  -i, --interactive       Pausa a cada iteração\n"
      "  -l, --list              Lista algoritmos disponíveis\n"
      "  -d, --dump-config       Serializa g_cfg após parse e encerra\n"
      "  -h, --help [ALG]        Ajuda geral ou de um algoritmo específico\n"
      "\n"
      "Campos comuns (válidos para qualquer algoritmo):\n");
  print_help_from_table(common_fields);
  printf("\nAlgoritmos:\n");
  for (int i = 0; algorithms[i].name; i++) printf("  %s\n", algorithms[i].name);
  printf("\nUse --help ALG para ver os campos de um algoritmo.\n");
}

static void print_help_algorithm(const char* name) {
  for (int i = 0; algorithms[i].name; i++) {
    if (strcmp(name, algorithms[i].name) == 0) {
      printf("Campos do algoritmo '%s' (configure via INI):\n", name);
      print_help_from_table(algorithms[i].fields);
      return;
    }
  }
  char msg[128];
  snprintf(msg, sizeof(msg), "algoritmo desconhecido: '%s'", name);
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
        snprintf(msg, sizeof(msg), "opção inválida: -%c", optopt);
        die(msg);
      }
    }
  }

  if (want_help) {
    if (optind < argc) print_help_algorithm(argv[optind]);
    else print_help_overview();
    exit(EXIT_SUCCESS);
  }

  if (optind < argc) die("argumento inesperado");
  if (!config_path) die("--config / -f é obrigatório");

  parse_ini(config_path);
  validate();
  rng_init_auto();

  if (dump_config) {
    dump_config_fn();
    exit(EXIT_SUCCESS);
  }
}
