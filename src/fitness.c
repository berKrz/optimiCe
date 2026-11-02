#include <math.h>
#include <stdio.h>

#include "common.h"

double sphere(const double* pos) {
  double sum = 0.0;
  for (int i = 0; i < g_cfg.n_dims; i++) sum += pos[i] * pos[i];
  return sum;
}

double rosenbrock(const double* pos) {
  if (g_cfg.n_dims < 2) die("para Rosenbrock, g_cfg.n_dims precisa ser maior que 1");
  double sum = 0.0;
  for (int i = 0; i < g_cfg.n_dims - 1; i++) {
    double t1  = pos[i + 1] - pos[i] * pos[i];
    double t2  = 1 - pos[i];
    sum       += 100.0 * t1 * t1 + t2 * t2;
  }
  return sum;
}

double rastrigin(const double* pos) {
  if (g_cfg.domain_min < -5.12 || g_cfg.domain_max > 5.12)
    die("para Rastrigin, cada dimensão deve estar no domínio [-5.12, 5.12]");

  double a = 10.0, sum = a * g_cfg.n_dims;
  for (int i = 0; i < g_cfg.n_dims; i++)
    sum += pos[i] * pos[i] - a * cos(2.0 * M_PI * pos[i]);
  return sum;
}

double griewank(const double* pos) {
  if (g_cfg.domain_min < -600.0 || g_cfg.domain_max > 600.0)
    die("para Griewank, cada dimensão deve estar no domínio [-600, 600]");

  double sum = 0.0, product = 1.0;
  for (int i = 0; i < g_cfg.n_dims; i++) {
    sum     += pos[i] * pos[i];
    product *= cos(pos[i] / sqrt((double) (i + 1)));
  }
  return 1.0 + sum / 4000.0 - product;
}

double ackley(const double* pos) {
  if (g_cfg.domain_min < -5.0 || g_cfg.domain_max > 5.0)
    die("para Ackley, cada dimensão deve estar no domínio [-5.0, 5.0]");

  const double a = 20.0, b = 0.2, c = 2.0 * M_PI;
  double       sum_sq = 0.0, sum_cos = 0.0;
  for (int i = 0; i < g_cfg.n_dims; i++) {
    sum_sq  += pos[i] * pos[i];
    sum_cos += cos(c * pos[i]);
  }
  return -a * exp(-b * sqrt(sum_sq / g_cfg.n_dims)) - exp(sum_cos / g_cfg.n_dims) + a +
         exp(1.0);
}

double eggholder(const double* pos) {
  if (g_cfg.n_dims != 2) die("para Eggholder, g_cfg.n_dims precisa ser igual a 2");
  if (g_cfg.domain_min < -512.0 || g_cfg.domain_max > 512.0)
    die("para Eggholder, cada dimensão deve estar no domínio [-512, 512]");

  double x = pos[0], y = pos[1];
  return -(y + 47.0) * sin(sqrt(fabs(x / 2.0 + (y + 47.0)))) -
         x * sin(sqrt(fabs(x - (y + 47.0))));
}

double schwefel(const double* pos) {
  if (g_cfg.domain_min < -500.0 || g_cfg.domain_max > 500.0)
    die("para Schwefel, cada dimensão deve estar no domínio [-500, 500]");

  const double a   = 418.9828872724338;
  double       sum = 0.0;
  for (int i = 0; i < g_cfg.n_dims; i++) sum += -pos[i] * sin(sqrt(fabs(pos[i])));
  return a * g_cfg.n_dims + sum;
}

double schaffer_f6(const double* pos) {
  if (g_cfg.n_dims != 2) die("para Schaffer's F6, g_cfg.n_dims precisa ser igual a 2");
  if (g_cfg.domain_min < -100.0 || g_cfg.domain_max > 100.0)
    die("para Schaffer's F6, cada dimensão deve estar no domínio [-100, 100]");

  double x = pos[0], y = pos[1], r1 = x * x + y * y, r2 = 1.0 + 0.001 * r1;
  return 0.5 + (sin(sqrt(r1)) * sin(sqrt(r1)) - 0.5) / (r2 * r2);
}

// Catalog of available fitness functions, by INI name. NULL-terminated.
// Read by config.c's "fitness" handler to resolve a string to a function pointer.
const FitnessEntry fitness_table[] = {
    {"sphere",      sphere     },
    {"rosenbrock",  rosenbrock },
    {"rastrigin",   rastrigin  },
    {"griewank",    griewank   },
    {"ackley",      ackley     },
    {"eggholder",   eggholder  },
    {"schwefel",    schwefel   },
    {"schaffer_f6", schaffer_f6},
    {NULL,          NULL       }
};
