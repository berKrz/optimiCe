#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "common.h"

static char* trim(char* s) {
  while (isspace((unsigned char) *s)) s++;
  if (*s == '\0') return s;
  char* end = s + strlen(s) - 1;
  while (end > s && isspace((unsigned char) *end)) end--;
  *(end + 1) = '\0';
  return s;
}

void parse_ini(const char* filename) {
  FILE* f = fopen(filename, "r");
  if (!f) {
    char msg[256];
    snprintf(msg, sizeof(msg), "não foi possível abrir '%s': %s", filename, strerror(errno));
    die(msg);
  }

  char line[256];
  int  linenum = 0;

  while (fgets(line, sizeof(line), f)) {
    linenum++;
    char* s = trim(line);
    if (*s == '\0' || *s == '#') continue;

    char* eq = strchr(s, '=');
    char  source[128];
    snprintf(source, sizeof(source), "%s:%d", filename, linenum);

    if (!eq) die_at(source, NULL, "formato esperado: 'chave = valor'");

    *eq       = '\0';
    char* key = trim(s);
    char* val = trim(eq + 1);

    // Accept both key-name and key_name forms.
    for (char* p = key; *p; p++)
      if (*p == '-') *p = '_';

    apply_field(source, key, val);
  }

  fclose(f);
}
