#include <criterion/criterion.h>
#include <criterion/new/assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include "unittest.h"

// --- config_stem ---

static void expect_stem(const char* path, const char* expected) {
  char* stem = config_stem(path);
  cr_expect(eq(str, stem, (char*) expected), "stem of '%s'", path);
  free(stem);
}

Test(output, config_stem) {
  expect_stem("dir/exp.toml", "exp");
  expect_stem("exp.toml", "exp");
  expect_stem("exp", "exp");
  expect_stem("a.b.toml", "a.b");
  expect_stem(".toml", ".toml");  // removing the extension would leave nothing
  expect_stem("x.TOML", "x.TOML");
  expect_stem("dir.toml/x", "x");
}

// --- open_generated ---

// Each test runs in a fresh temporary directory; Criterion runs every test in its own process,
// so changing directory doesn't leak
static void enter_temp_dir(void) {
  char dir[] = "/tmp/optimice-test-XXXXXX";
  cr_assert(mkdtemp(dir) != NULL, "can't create a temporary directory");
  cr_assert(eq(int, chdir(dir), 0));
}

TestSuite(open_generated, .init = enter_temp_dir);

static const struct tm when = {
    .tm_year = 126, .tm_mon = 10, .tm_mday = 16, .tm_hour = 14, .tm_min = 5, .tm_sec = 9};

static int exists(const char* path) {
  struct stat st;
  return stat(path, &st) == 0;
}

// The same config in the same second: -2, -3, ... rather than replacing anything
Test(open_generated, numbers_repeated_names) {
  for (int k = 0; k < 3; k++) fclose(open_generated("", "configs/exp.toml", &when));
  cr_expect(exists("exp-20261116-140509.csv"));
  cr_expect(exists("exp-20261116-140509-2.csv"));
  cr_expect(exists("exp-20261116-140509-3.csv"));
  cr_expect(exists("exp-20261116-140509-4.csv") == 0);
}

Test(open_generated, leaves_existing_file) {
  FILE* f = fopen("exp-20261116-140509.csv", "w");
  fputs("keep\n", f);
  fclose(f);

  fclose(open_generated("", "exp.toml", &when));
  cr_expect(exists("exp-20261116-140509-2.csv"));

  char line[16] = {0};
  f             = fopen("exp-20261116-140509.csv", "r");
  cr_assert(fgets(line, sizeof(line), f) != NULL);
  fclose(f);
  cr_expect(eq(str, line, "keep\n"));
}

Test(open_generated, inside_directory) {
  cr_assert(eq(int, mkdir("out", 0777), 0));
  fclose(open_generated("out/", "exp.toml", &when));
  cr_expect(exists("out/exp-20261116-140509.csv"));
}
