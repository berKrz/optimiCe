#include "helpers.h"

#include <criterion/criterion.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

Batch load_toml(const char* toml) {
  char path[] = "/tmp/optimice-test-XXXXXX";
  int  fd     = mkstemp(path);
  cr_assert(fd >= 0, "can't create a temporary config");
  size_t len = strlen(toml);
  cr_assert(write(fd, toml, len) == (ssize_t) len, "can't write the temporary config");
  close(fd);

  Batch batch = load_batch(path);
  unlink(path);
  return batch;
}

void* algo_field_in(const Algorithm* algo, void* base, const char* key) {
  for (const FieldSpec* f = algo->fields; f->name; f++)
    if (strcmp(f->name, key) == 0) return (char*) base + f->offset;
  cr_assert_fail("%s has no field '%s'", algo->name, key);
  return NULL;
}

void* algo_field(const RunConfig* rc, const char* key) {
  return algo_field_in(rc->cfg.algorithm, rc->algo_cfg, key);
}
