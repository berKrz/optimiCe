#include <math.h>
#include <stdio.h>
#include <string.h>

#include "core/core.h"

// A catalog entry. The struct is defined here and nowhere else, so no other file can reach `fn`:
// evaluate is the only caller of a fitness function, and no call goes uncounted
struct FitnessEntry {
  const char* name;
  double (*fn)(const double*);
  // n_dims_min == n_dims_max for fixed dimension functions
  int    n_dims_min;  // 0 = unbounded below
  int    n_dims_max;  // 0 = unbounded above
  double domain_lo;   // -INFINITY = unbounded below
  double domain_hi;   //  INFINITY = unbounded above
};

// --- Counted evaluation ---

static _Thread_local uint64_t count;

double evaluate(const double* pos) {
  count++;
  return g_cfg.fitness->fn(pos);
}

void     evaluations_reset(void) { count = 0; }
uint64_t evaluations(void) { return count; }

// --- Functions ---

static double sphere(const double* pos) {
  double sum = 0.0;
  for (int i = 0; i < g_cfg.n_dims; i++) sum += pos[i] * pos[i];
  return sum;
}

static double rosenbrock(const double* pos) {
  double sum = 0.0;
  for (int i = 0; i < g_cfg.n_dims - 1; i++) {
    double t1  = pos[i + 1] - pos[i] * pos[i];
    double t2  = 1 - pos[i];
    sum       += 100.0 * t1 * t1 + t2 * t2;
  }
  return sum;
}

static double rastrigin(const double* pos) {
  double a = 10.0, sum = a * g_cfg.n_dims;
  for (int i = 0; i < g_cfg.n_dims; i++) sum += pos[i] * pos[i] - a * cos(2.0 * M_PI * pos[i]);
  return sum;
}

static double griewank(const double* pos) {
  double sum = 0.0, product = 1.0;
  for (int i = 0; i < g_cfg.n_dims; i++) {
    sum     += pos[i] * pos[i];
    product *= cos(pos[i] / sqrt((double) (i + 1)));
  }
  return 1.0 + sum / 4000.0 - product;
}

static double ackley(const double* pos) {
  const double a = 20.0, b = 0.2, c = 2.0 * M_PI;
  double       sum_sq = 0.0, sum_cos = 0.0;
  for (int i = 0; i < g_cfg.n_dims; i++) {
    sum_sq  += pos[i] * pos[i];
    sum_cos += cos(c * pos[i]);
  }
  return -a * exp(-b * sqrt(sum_sq / g_cfg.n_dims)) - exp(sum_cos / g_cfg.n_dims) + a + exp(1.0);
}

static double eggholder(const double* pos) {
  double x = pos[0], y = pos[1];
  return -(y + 47.0) * sin(sqrt(fabs(x / 2.0 + (y + 47.0)))) - x * sin(sqrt(fabs(x - (y + 47.0))));
}

static double schwefel(const double* pos) {
  const double a   = 418.9828872724338;
  double       sum = 0.0;
  for (int i = 0; i < g_cfg.n_dims; i++) sum += -pos[i] * sin(sqrt(fabs(pos[i])));
  return a * g_cfg.n_dims + sum;
}

static double schaffer_f6(const double* pos) {
  double x = pos[0], y = pos[1], r1 = x * x + y * y, r2 = 1.0 + 0.001 * r1;
  return 0.5 + (sin(sqrt(r1)) * sin(sqrt(r1)) - 0.5) / (r2 * r2);
}

// --- Catalog ---

// The available fitness functions, by config name; NULL-terminated
// A function must not return NaN for finite coordinates: nothing compares better or worse than
// NaN, and the algorithms' best tracking relies on that. Overflowing to an infinity is fine
static const FitnessEntry fitness_table[] = {
    {"sphere",      sphere,      0, 0, -INFINITY, INFINITY},
    {"rosenbrock",  rosenbrock,  2, 0, -INFINITY, INFINITY},
    {"rastrigin",   rastrigin,   0, 0, -5.12,     5.12    },
    {"griewank",    griewank,    0, 0, -600.0,    600.0   },
    {"ackley",      ackley,      0, 0, -5.0,      5.0     },
    {"eggholder",   eggholder,   2, 2, -512.0,    512.0   },
    {"schwefel",    schwefel,    0, 0, -500.0,    500.0   },
    {"schaffer_f6", schaffer_f6, 2, 2, -100.0,    100.0   },
    {NULL,          NULL,        0, 0, 0.0,       0.0     },
};

const FitnessEntry* fitness_find(const char* name) {
  for (int i = 0; fitness_table[i].name; i++)
    if (strcmp(fitness_table[i].name, name) == 0) return &fitness_table[i];
  return NULL;
}

const char* fitness_name(const FitnessEntry* entry) { return entry->name; }

void fitness_check(void) {
  const FitnessEntry* fe = g_cfg.fitness;

  if (fe->n_dims_min && g_cfg.n_dims < fe->n_dims_min)
    die_with_hint("for %s, n_dims must be at least %d (current: %d)", fe->name, fe->n_dims_min,
                  g_cfg.n_dims);
  if (fe->n_dims_max && g_cfg.n_dims > fe->n_dims_max)
    die_with_hint("for %s, n_dims must be at most %d (current: %d)", fe->name, fe->n_dims_max,
                  g_cfg.n_dims);
  if (g_cfg.domain_min < fe->domain_lo || g_cfg.domain_max > fe->domain_hi)
    die_with_hint("for %s, the domain must be contained in [%g, %g]", fe->name, fe->domain_lo,
                  fe->domain_hi);
}

void fitness_print_catalog(void) {
  for (const FitnessEntry* fe = fitness_table; fe->name; fe++) {
    char dims[32] = "any", domain[64] = "any";
    if (fe->n_dims_min && fe->n_dims_min == fe->n_dims_max)
      snprintf(dims, sizeof(dims), "%d", fe->n_dims_min);
    else if (fe->n_dims_min && fe->n_dims_max)
      snprintf(dims, sizeof(dims), "%d to %d", fe->n_dims_min, fe->n_dims_max);
    else if (fe->n_dims_min) snprintf(dims, sizeof(dims), "%d or more", fe->n_dims_min);
    else if (fe->n_dims_max) snprintf(dims, sizeof(dims), "%d or fewer", fe->n_dims_max);
    if (isfinite(fe->domain_lo) || isfinite(fe->domain_hi))
      snprintf(domain, sizeof(domain), "within [%g, %g]", fe->domain_lo, fe->domain_hi);
    printf("  %-12s  n_dims: %-10s  domain: %s\n", fe->name, dims, domain);
  }
}
