#include <stdio.h>
#include <stdlib.h>

#include "common.h"

void die(const char* msg) {
  fprintf(stderr, "Erro: %s\n", msg);
  fprintf(stderr, "Execute com --help para ver as opcoes.\n");
  exit(EXIT_FAILURE);
}

void die_at(const char* source, const char* key, const char* msg) {
  if (key) fprintf(stderr, "Erro em '%s' [%s]: %s\n", source, key, msg);
  else fprintf(stderr, "Erro em '%s': %s\n", source, msg);
  fprintf(stderr, "Execute com --help para ver as opcoes.\n");
  exit(EXIT_FAILURE);
}

void clear_screen(void) {
  printf("\033[2J\033[H");
  fflush(stdout);
}

void wait_for_enter(void) {
  if (!g_cfg.interactive) return;
  printf("\n  [Enter para continuar...]\n");
  while (getchar() != '\n');
}

void print_separator(void) {
  printf("\n");
  for (int i = 0; i < LINE_WIDTH / 2; i++) printf("- ");
  printf("\n\n");
}

void print_iteration_header(int iter) {
  char buf[32];
  int  n = snprintf(buf, sizeof(buf), "=== Iteracao %d ", iter);
  printf("%s", buf);
  for (int i = n; i < LINE_WIDTH; i++) printf("=");
  printf("\n\n");
}
