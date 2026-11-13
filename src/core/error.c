#include <limits.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "core/core.h"

_Thread_local const char* g_where;

// Several worker threads may fail at once, but exit() must only run once: the first caller
// reports and exits, which ends every thread; any later caller blocks until that happens
static atomic_flag exiting = ATOMIC_FLAG_INIT;

static void claim_exit(void) {
  if (atomic_flag_test_and_set(&exiting))
    for (;;) pause();
}

// The one exit path behind the die family: "Error[ at '<source>'[ [<key>]]]: <message>", then
// the --help hint when asked for
static noreturn void die_va(const char* source, const char* key, int hint, const char* fmt,
                            va_list args) {
  claim_exit();
  fprintf(stderr, "Error");
  if (source) fprintf(stderr, " at '%s'", source);
  if (key) fprintf(stderr, " [%s]", key);
  fprintf(stderr, ": ");
  vfprintf(stderr, fmt, args);
  fprintf(stderr, "\n");
  if (hint) fprintf(stderr, "Run with --help to see available options.\n");
  exit(EXIT_FAILURE);
}

void die(const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  die_va(g_where, NULL, 0, fmt, args);
}

void die_with_hint(const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  die_va(g_where, NULL, 1, fmt, args);
}

void die_at(const char* source, const char* key, const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  die_va(source, key, 1, fmt, args);
}

void* xmalloc(size_t size) {
  void* p = malloc(size);
  if (!p) die("memory allocation failed");
  return p;
}

void* xcalloc(size_t n, size_t size) {
  void* p = calloc(n, size);
  if (!p) die("memory allocation failed");
  return p;
}

void check_fits_int(int count, long long per_item, const char* what) {
  if (per_item <= INT_MAX && count <= INT_MAX / per_item) return;
  die_with_hint("%s is too large (at most %d elements per buffer)", what, INT_MAX);
}
