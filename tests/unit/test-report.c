#include <criterion/criterion.h>
#include <criterion/new/assert.h>
#include <float.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "unittest.h"

static void expect_text(double x, const char* expected) {
  char buf[FMT_EXACT_LEN];
  fmt_exact(buf, x);
  cr_expect(eq(str, buf, (char*) expected));
}

// --- Exact strings ---

Test(fmt_exact, known_values) {
  expect_text(0.1, "0.1");
  expect_text(1.0 / 3.0, "0.3333333333333333");
  expect_text(100.0, "100");
  expect_text(1e21, "1e+21");
  expect_text(-0.0, "-0");
  expect_text(INFINITY, "inf");
  expect_text(-INFINITY, "-inf");
  expect_text(NAN, "nan");
}

// --- Exact and short ---

// Reads back as the very same double, sign of zero included. When it takes more than the 6 digits
// fmt_exact starts at, one digit fewer wouldn't have read back
static void check_exact_and_short(double x) {
  char buf[FMT_EXACT_LEN], shorter[FMT_EXACT_LEN];
  fmt_exact(buf, x);
  double back = strtod(buf, NULL);
  cr_assert(memcmp(&back, &x, sizeof(x)) == 0, "%a printed as %s, read back as %a", x, buf, back);

  int p = 6;
  for (; p <= 17; p++) {
    snprintf(shorter, sizeof(shorter), "%.*g", p, x);
    if (strcmp(shorter, buf) == 0) break;
  }
  cr_assert(p <= 17, "%a printed as %s, which no %%g precision gives", x, buf);
  if (p > 6) {
    snprintf(shorter, sizeof(shorter), "%.*g", p - 1, x);
    cr_assert(strtod(shorter, NULL) != x, "%a printed as %s, but %s reads back too", x, buf,
              shorter);
  }
}

Test(fmt_exact, special_values) {
  const double values[] = {0.0,     -0.0,      DBL_MIN,      -DBL_MIN,
                           DBL_MAX, -DBL_MAX,  DBL_TRUE_MIN, 1e-310,
                           0.1,     1.0 / 3.0, 2.0 / 3.0,    123456789012345678.0};
  for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); i++) check_exact_and_short(values[i]);
}

// Any finite double, from random bit patterns
Test(fmt_exact, random_doubles) {
  rng_init(1);
  for (int i = 0; i < 10000; i++) {
    uint64_t bits = rng_next_uint64(UINT64_C(1) << 32) << 32 | rng_next_uint64(UINT64_C(1) << 32);
    double   x;
    memcpy(&x, &bits, sizeof(x));
    if (isfinite(x)) check_exact_and_short(x);
  }
}
