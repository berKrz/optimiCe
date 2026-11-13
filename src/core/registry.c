#include <string.h>

#include "algorithms.h"

// Defines `algorithms[]`, the NULL-terminated registry: one entry per file in src/algorithms/, in
// alphabetical order. The Makefile writes the header from the file names
#include "registry.h"

int find_algorithm(const char* name) {
  for (int i = 0; algorithms[i]; i++)
    if (strcmp(name, algorithms[i]->name) == 0) return i;
  return -1;
}
