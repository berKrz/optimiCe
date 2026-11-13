#include <math.h>
#include <stddef.h>
#include <stdio.h>

#include "algorithms.h"
#include "common.h"

_Thread_local Config g_cfg;

// --- Enum lookup tables ---

static const EnumEntry direction_table[] = {
    {"minimize", MINIMIZE},
    {"maximize", MAXIMIZE},
    {NULL,       0       },
};

// --- Runner field table ---

// Row order is --help order: [common]-only fields first, [[run]]-only last
const FieldSpec runner_fields[] = {
    FIELD_ROW(Config, seed, UINT, SECTION_COMMON, NULL, NULL,
              "Base seed; replicate r uses seed + r (omitted = generated)"),
    FIELD_ROW(Config, replicates, INT, SECTION_COMMON, NULL, "1",
              "Repetitions of every run, one seed each"),
    FIELD_ROW(Config, direction, ENUM, SCOPE_GLOBAL, direction_table, "minimize", "Direction"),
    FIELD_ROW(Config, n_dims, INT, SCOPE_GLOBAL, NULL, "2", "Number of dimensions"),
    FIELD_ROW(Config, iterations, INT, SCOPE_GLOBAL, NULL, "100",
              "Iteration budget (generations, for GA)"),
    FIELD_ROW(Config, target_fitness, DOUBLE, SCOPE_GLOBAL, NULL, "nan",
              "Stop early once the best reaches this value"),
    FIELD_ROW(Config, stagnation, INT, SCOPE_GLOBAL, NULL, "0",
              "Stop early after N iterations without improvement (0 = off)"),
    FIELD_ROW(Config, domain_min, DOUBLE, SCOPE_GLOBAL, NULL, "-5.0",
              "Lower bound of the domain (-inf = unbounded below)"),
    FIELD_ROW(Config, domain_max, DOUBLE, SCOPE_GLOBAL, NULL, "5.0",
              "Upper bound of the domain (inf = unbounded above)"),
    FIELD_ROW(Config, init_min, DOUBLE, SCOPE_GLOBAL, NULL, "nan",
              "Lower end of the initial population's range (unset = domain_min)"),
    FIELD_ROW(Config, init_max, DOUBLE, SCOPE_GLOBAL, NULL, "nan",
              "Upper end of the initial population's range (unset = domain_max)"),
    FIELD_ROW(Config, fitness, FITNESS, SCOPE_GLOBAL, NULL, "sphere",
              "Fitness function (see --list)"),
    FIELD_ROW(Config, algorithm, ALGORITHM, SECTION_RUN, NULL, NULL,
              "Algorithm to run (see --list)"),
    FIELDS_END,
};

// --- Validation ---

void validate(void) {
  if (!g_cfg.algorithm) die_with_hint("algorithm not set in config file");
  if (g_cfg.n_dims < 1) die_with_hint("n_dims must be at least 1");
  // Either end of the domain may be infinite: the search is unbounded on that side
  // Two finite ends can still be too far apart to subtract
  if (g_cfg.domain_min >= g_cfg.domain_max)
    die_with_hint("domain_min must be strictly less than domain_max");
  if (isfinite(g_cfg.domain_min) && isfinite(g_cfg.domain_max) &&
      !isfinite(g_cfg.domain_max - g_cfg.domain_min))
    die_with_hint("the domain is too wide: domain_max - domain_min must be finite");

  // Not a check: an unset end of the init range is the domain's, resolved here so dump and run
  // see it. An infinite end of the domain can't stand in: the init range is what uniform draws
  // sample from, so it has to be finite, and so does its width
  if (isnan(g_cfg.init_min)) {
    if (isinf(g_cfg.domain_min)) die_with_hint("domain_min is infinite, so init_min must be set");
    g_cfg.init_min = g_cfg.domain_min;
  }
  if (isnan(g_cfg.init_max)) {
    if (isinf(g_cfg.domain_max)) die_with_hint("domain_max is infinite, so init_max must be set");
    g_cfg.init_max = g_cfg.domain_max;
  }
  if (!isfinite(g_cfg.init_min) || !isfinite(g_cfg.init_max))
    die_with_hint("init_min and init_max must be finite");
  if (g_cfg.init_min >= g_cfg.init_max)
    die_with_hint("init_min must be strictly less than init_max");
  if (!isfinite(g_cfg.init_max - g_cfg.init_min))
    die_with_hint("the init range is too wide: init_max - init_min must be finite");
  if (g_cfg.init_min < g_cfg.domain_min || g_cfg.init_max > g_cfg.domain_max)
    die_with_hint("the init range must be inside the domain");

  if (g_cfg.iterations < 1) die_with_hint("iterations must be positive");
  if (g_cfg.stagnation < 0) die_with_hint("stagnation must be non-negative (0 disables it)");

  fitness_check();  // g_cfg.fitness is set: set_field resolved it, or the default did

  if (g_cfg.algorithm->validate) g_cfg.algorithm->validate();
}

// One run's resolved config, as loaded into g_cfg and the algorithm's cfg
void dump_config(void) {
  dump_fields(runner_fields, &g_cfg);
  printf("\n# %s fields\n", g_cfg.algorithm->name);
  dump_fields(g_cfg.algorithm->fields, g_cfg.algorithm->cfg_base());
}
