#include <math.h>

#include "common.h"

// --- Counted evaluation ---

static _Thread_local uint64_t count;

double evaluate(const double* pos) {
  count++;
  return g_cfg.fitness_fn(pos);
}

void     evaluations_reset(void) { count = 0; }
uint64_t evaluations(void) { return count; }

// --- Functions ---

double sphere(const double* pos) {
  double sum = 0.0;
  for (int i = 0; i < g_cfg.n_dims; i++) sum += pos[i] * pos[i];
  return sum;
}

double rosenbrock(const double* pos) {
  double sum = 0.0;
  for (int i = 0; i < g_cfg.n_dims - 1; i++) {
    double t1  = pos[i + 1] - pos[i] * pos[i];
    double t2  = 1 - pos[i];
    sum       += 100.0 * t1 * t1 + t2 * t2;
  }
  return sum;
}

double rastrigin(const double* pos) {
  double a = 10.0, sum = a * g_cfg.n_dims;
  for (int i = 0; i < g_cfg.n_dims; i++) sum += pos[i] * pos[i] - a * cos(2.0 * M_PI * pos[i]);
  return sum;
}

double griewank(const double* pos) {
  double sum = 0.0, product = 1.0;
  for (int i = 0; i < g_cfg.n_dims; i++) {
    sum     += pos[i] * pos[i];
    product *= cos(pos[i] / sqrt((double) (i + 1)));
  }
  return 1.0 + sum / 4000.0 - product;
}

double ackley(const double* pos) {
  const double a = 20.0, b = 0.2, c = 2.0 * M_PI;
  double       sum_sq = 0.0, sum_cos = 0.0;
  for (int i = 0; i < g_cfg.n_dims; i++) {
    sum_sq  += pos[i] * pos[i];
    sum_cos += cos(c * pos[i]);
  }
  return -a * exp(-b * sqrt(sum_sq / g_cfg.n_dims)) - exp(sum_cos / g_cfg.n_dims) + a + exp(1.0);
}

double eggholder(const double* pos) {
  double x = pos[0], y = pos[1];
  return -(y + 47.0) * sin(sqrt(fabs(x / 2.0 + (y + 47.0)))) - x * sin(sqrt(fabs(x - (y + 47.0))));
}

double schwefel(const double* pos) {
  const double a   = 418.9828872724338;
  double       sum = 0.0;
  for (int i = 0; i < g_cfg.n_dims; i++) sum += -pos[i] * sin(sqrt(fabs(pos[i])));
  return a * g_cfg.n_dims + sum;
}

double schaffer_f6(const double* pos) {
  double x = pos[0], y = pos[1], r1 = x * x + y * y, r2 = 1.0 + 0.001 * r1;
  return 0.5 + (sin(sqrt(r1)) * sin(sqrt(r1)) - 0.5) / (r2 * r2);
}

// Catalog of available fitness functions, by config name. NULL-terminated
// Read by config.c's "fitness" handler to resolve a string to a function pointer,
// and by validate() to enforce per-function preconditions on g_cfg.
//
// n_dims_min & n_dims_max: 0 means unbounded on that side.
// domain_lo & domain_hi:   -/+INFINITY mean unbounded on that side.
const FitnessEntry fitness_table[] = {
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
